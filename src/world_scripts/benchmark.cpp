module;

#include <json/value.h>

module benchmark;

import std.compat;
import fixed_string;

import glm;
import rhmath;
import ecs;
import framework;
import locomotion;
import profile;
import gpu_profile;
import render;
import globals;
import cvar;
import json_utils;
import paths;
import log;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogBenchmark, Log);


namespace
{
    constexpr float PI = 3.14159265358979f;

    // CPU scopes deeper than this are not recorded per frame (the render graph passes are at depth 4)
    constexpr int max_scope_depth = 5;
    // the character is put on the waypoint when it got no closer to it for this long (an obstacle on the route)
    constexpr double stuck_seconds = 3.0;
    // camera turn towards the next leg of the route, rad/s
    constexpr float travel_turn_rate = 1.6f;
    // frames recorded after the run: the GPU timings of the last frames arrive late
    constexpr uint32_t drain_frame_count = 4;

    float wrap_angle(float a)
    {
        while (a > PI) a -= 2.0f * PI;
        while (a < -PI) a += 2.0f * PI;
        return a;
    }

    // a CSV field without the separator
    std::string csv_name(std::string_view text)
    {
        std::string result(text);
        std::ranges::replace(result, ',', ';');
        return result;
    }
}

bool Benchmark::start(World& world, const std::string& route_asset_path, const std::string& output_name,
    uint32_t laps_override, bool quit)
{
    const std::optional<Json::Value> route = json_utils::load_json_asset(route_asset_path);
    if (!route || !(*route)["waypoints"].isArray() || (*route)["waypoints"].size() < 2)
    {
        cvar::print(std::format("bench.run: '{}' has no waypoints", route_asset_path), cvar::Output::error);
        return false;
    }

    *this = {};
    route_path = route_asset_path;
    name = output_name;
    quit_when_done = quit;

    for (const Json::Value& point : (*route)["waypoints"])
        waypoints.push_back({ point[0].asFloat(), point[1].asFloat() });
    laps = laps_override > 0 ? laps_override : std::max((*route).get("laps", 1).asUInt(), 1u);
    arrive_distance = (*route).get("arrive_distance", arrive_distance).asFloat();
    warmup_seconds = (*route).get("warmup_seconds", warmup_seconds).asFloat();

    const Json::Value& camera = (*route)["camera"];
    sweep = glm::radians(camera.get("sweep_degrees", 0.0f).asFloat());
    sweep_period = std::max(camera.get("sweep_period", sweep_period).asFloat(), 0.1f);
    base_pitch = glm::radians(camera.get("pitch_degrees", glm::degrees(base_pitch)).asFloat());
    pitch_sweep = glm::radians(camera.get("pitch_sweep_degrees", 0.0f).asFloat());
    pitch_period = std::max(camera.get("pitch_period", pitch_period).asFloat(), 0.1f);

    // dropped from above the ground: it lands during the warmup
    cvar::execute(std::format("player.teleport {} {} {}", waypoints[0].x, (*route).get("start_height", 1.0f).asFloat(),
        waypoints[0].y));
    loco::set_scripted_locomotion_input(loco::LocomotionInput{});

    const glm::vec2 first_leg = waypoints[1] - waypoints[0];
    travel_yaw = std::atan2(-first_leg.x, -first_leg.y);
    next_waypoint = 1;
    start_time = world.get_time_seconds();
    state = State::warmup;

    LogBenchmark.Log("Benchmark '%s': %zu waypoints, %u laps, warmup %.1f s", route_path.c_str(), waypoints.size(), laps,
        warmup_seconds);
    return true;
}

void Benchmark::begin_run(World& world)
{
    state = State::running;
    start_time = world.get_time_seconds();
    start_wall = std::chrono::steady_clock::now();
    start_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    best_distance = std::numeric_limits<float>::max();
    best_distance_time = start_time;

    prof::set_is_profiling(true);
    gpuprof::clear_results();
    gpuprof::set_enabled(true);

    // earlier profiling sessions left their totals in the scopes: only what the run adds counts
    snapshot_scopes(prof::get_context().root, -1, 0, nullptr);
}

void Benchmark::tick(World& world, ecs::Entity character, float& yaw, float& pitch)
{
    if (state == State::idle)
        return;
    if (!character || !world.registry.get<loco::LocomotionCharacter>(character))
    {
        cvar::print("bench.run: no player character", cvar::Output::error);
        loco::set_scripted_locomotion_input(std::nullopt);
        state = State::idle;
        return;
    }

    // the frame which ended now is charged to the record opened at its start
    if (frame_open)
        close_frame();

    if (state == State::warmup)
    {
        yaw = travel_yaw;
        pitch = base_pitch;
        if (world.get_time_seconds() - start_time < warmup_seconds)
            return;
        begin_run(world);
    }

    if (state == State::draining)
    {
        if (++drain_frames >= drain_frame_count)
            finish();
        return;
    }

    steer(world, character, yaw, pitch);
    if (state == State::running)
        open_frame(world, character, yaw, pitch);
}

void Benchmark::steer(World& world, ecs::Entity character, float& yaw, float& pitch)
{
    const loco::LocomotionCharacter& movement = *world.registry.get<loco::LocomotionCharacter>(character);
    const double now = world.get_time_seconds();
    const float dt = float(std::min(world.get_delta_seconds(), 0.1));

    const glm::vec2 position(movement.position.x, movement.position.z);
    glm::vec2 to_target = waypoints[next_waypoint] - position;
    float distance = glm::length(to_target);

    bool advance = distance < arrive_distance;
    if (distance < best_distance - 0.25f)
    {
        best_distance = distance;
        best_distance_time = now;
    }
    else if (now - best_distance_time > stuck_seconds)
    {
        ++teleports;
        LogBenchmark.Log("Benchmark: stuck at (%.1f, %.1f) on the way to waypoint %u (%.1f, %.1f), teleporting",
            position.x, position.y, next_waypoint, waypoints[next_waypoint].x, waypoints[next_waypoint].y);
        cvar::execute(std::format("player.teleport {} {} {}", waypoints[next_waypoint].x, movement.position.y + 1.0f,
            waypoints[next_waypoint].y));
        advance = true;
    }

    if (advance)
    {
        // the loop closes on waypoint 0
        if (next_waypoint == 0 && ++lap >= laps)
        {
            loco::set_scripted_locomotion_input(loco::LocomotionInput{});
            state = State::draining;
            drain_frames = 0;
            return;
        }
        next_waypoint = (next_waypoint + 1) % uint32_t(waypoints.size());
        best_distance = std::numeric_limits<float>::max();
        best_distance_time = now;
        to_target = waypoints[next_waypoint] - position;
        distance = glm::length(to_target);
    }

    const glm::vec2 direction = distance > 1e-3f ? to_target / distance : glm::vec2(0.0f, -1.0f);

    // the camera follows the route and looks around it: left and right of the way, up and down
    const float target_yaw = std::atan2(-direction.x, -direction.y);
    const float turn = wrap_angle(target_yaw - travel_yaw);
    travel_yaw = wrap_angle(travel_yaw + std::clamp(turn, -travel_turn_rate * dt, travel_turn_rate * dt));

    const float t = float(now - start_time);
    yaw = travel_yaw + sweep * std::sin(2.0f * PI * t / sweep_period);
    pitch = base_pitch + pitch_sweep * std::sin(2.0f * PI * t / pitch_period);

    // the input is relative to the camera (module locomotion: view yaw of the player's camera)
    const glm::vec2 camera_forward(-std::sin(yaw), -std::cos(yaw));
    const glm::vec2 camera_right(std::cos(yaw), -std::sin(yaw));
    loco::LocomotionInput input;
    input.move_axis = glm::vec2(glm::dot(direction, camera_right), glm::dot(direction, camera_forward));
    loco::set_scripted_locomotion_input(input);
}

void Benchmark::open_frame(World& world, ecs::Entity character, float yaw, float pitch)
{
    const loco::LocomotionCharacter& movement = *world.registry.get<loco::LocomotionCharacter>(character);
    frame_wall = std::chrono::steady_clock::now();

    Frame& frame = frames.emplace_back();
    frame.time = world.get_time_seconds() - start_time;
    frame.wall = std::chrono::duration<double>(frame_wall - start_wall).count();
    frame.position = movement.position;
    frame.yaw = yaw;
    frame.pitch = pitch;
    frame.lap = lap;
    frame.waypoint = next_waypoint;
    frame_open = true;
}

void Benchmark::close_frame()
{
    Frame& frame = frames.back();
    frame.frame_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frame_wall).count();
    // this tick runs before the renderer: the last recorded GPU frame is the one of the record
    frame.gpu_frame = gpuprof::current_frame_id();

    for (const auto& [counter, value] : prof::get_counters().last_frame)
    {
        size_t index = std::ranges::find(counter_names, counter) - counter_names.begin();
        if (index == counter_names.size())
            counter_names.push_back(counter);
        if (frame.counters.size() <= index)
            frame.counters.resize(index + 1, 0);
        frame.counters[index] = value;
    }

    snapshot_scopes(prof::get_context().root, -1, 0, &frame.scopes_ms);
    frame_open = false;
}

void Benchmark::snapshot_scopes(const prof::ProfileNode& node, int32_t parent, int depth, std::vector<float>* out)
{
    for (const auto& [scope_name, child] : node.children)
    {
        auto it = scope_index.find(child);
        if (it == scope_index.end())
        {
            it = scope_index.emplace(child, uint32_t(scopes.size())).first;
            scopes.push_back({ parent < 0 ? std::string(scope_name) : scopes[parent].path + "/" + std::string(scope_name) });
        }
        const uint32_t index = it->second;

        const int64_t ns = child->inclusive_time.count();
        if (out)
        {
            const float ms = float(double(ns - scopes[index].last_ns) * 1e-6);
            if (out->size() <= index)
                out->resize(index + 1, 0.0f);
            (*out)[index] = ms;
            scopes[index].total_ms += ms;
        }
        scopes[index].last_ns = ns;

        if (depth + 1 < max_scope_depth)
            snapshot_scopes(*child, int32_t(index), depth + 1, out);
    }
}

void Benchmark::finish()
{
    state = State::idle;
    loco::set_scripted_locomotion_input(std::nullopt);
    prof::set_is_profiling(false);
    gpuprof::set_enabled(false);

    write_results();

    double total_ms = 0.0;
    for (const Frame& frame : frames)
        total_ms += frame.frame_ms;
    LogBenchmark.Log("Benchmark '%s' finished: %zu frames in %.1f s (%.1f FPS on average), %u teleports -> cache/bench/%s_*",
        route_path.c_str(), frames.size(), total_ms * 1e-3, frames.empty() ? 0.0 : 1000.0 * double(frames.size()) / total_ms,
        teleports, name.c_str());

    if (quit_when_done)
        cvar::execute("quit");
}

void Benchmark::write_results() const
{
    const std::filesystem::path dir = paths::get_cache_path() / "bench";
    std::filesystem::create_directories(dir);

    // scopes which took no time (one-off loading scopes, garbage of the frame the profiler was switched on in)
    std::vector<uint32_t> scope_columns;
    for (uint32_t index = 0; index < scopes.size(); ++index)
        if (!frames.empty() && scopes[index].total_ms / double(frames.size()) >= 0.005)
            scope_columns.push_back(index);

    {
        std::ofstream file(dir / (name + "_frames.csv"));
        file << "frame,time,wall,frame_ms,x,y,z,yaw,pitch,lap,waypoint,gpu_frame";
        for (const std::string& counter : counter_names)
            file << ",count:" << csv_name(counter);
        for (uint32_t index : scope_columns)
            file << ",cpu:" << csv_name(scopes[index].path);
        file << "\n";

        for (size_t i = 0; i < frames.size(); ++i)
        {
            const Frame& frame = frames[i];
            file << std::format("{},{:.4f},{:.4f},{:.3f},{:.2f},{:.2f},{:.2f},{:.4f},{:.4f},{},{},{}", i, frame.time, frame.wall,
                frame.frame_ms, frame.position.x, frame.position.y, frame.position.z, frame.yaw, frame.pitch, frame.lap,
                frame.waypoint, frame.gpu_frame);
            for (size_t index = 0; index < counter_names.size(); ++index)
                file << ',' << (index < frame.counters.size() ? frame.counters[index] : 0);
            for (uint32_t index : scope_columns)
                file << std::format(",{:.3f}", index < frame.scopes_ms.size() ? frame.scopes_ms[index] : 0.0f);
            file << "\n";
        }
    }

    {
        // a pass may run several times in a frame (layers), or not at all: summed by frame id
        const auto& results = gpuprof::ctx().results;
        std::vector<std::string> passes;
        for (const auto& [pass, result] : results)
            passes.push_back(pass);
        std::ranges::sort(passes);

        std::map<uint64_t, std::vector<double>> rows;
        for (size_t column = 0; column < passes.size(); ++column)
        {
            const gpuprof::PassResult& result = results.at(passes[column]);
            for (size_t i = 0; i < result.samples.size(); ++i)
            {
                std::vector<double>& row = rows[result.frames[i]];
                row.resize(passes.size(), 0.0);
                row[column] += result.samples[i];
            }
        }

        std::ofstream file(dir / (name + "_gpu.csv"));
        file << "gpu_frame";
        for (const std::string& pass : passes)
            file << ',' << csv_name(pass);
        file << "\n";
        for (const auto& [frame, row] : rows)
        {
            file << frame;
            for (double ms : row)
                file << std::format(",{:.4f}", ms);
            file << "\n";
        }
    }

    const Extent extent = RhGlobals::engine->renderer->get_backend()->get_swapchain_extent();
    Json::Value meta(Json::objectValue);
    meta["route"] = route_path;
    meta["width"] = extent.width;
    meta["height"] = extent.height;
    meta["laps"] = laps;
    meta["frames"] = Json::UInt64(frames.size());
    meta["teleports"] = teleports;
    meta["start_unix_ms"] = Json::Int64(start_unix_ms);
    Json::Value points(Json::arrayValue);
    for (const glm::vec2& waypoint : waypoints)
    {
        Json::Value point(Json::arrayValue);
        point.append(waypoint.x);
        point.append(waypoint.y);
        points.append(point);
    }
    meta["waypoints"] = points;
    json_utils::save_json_to_path(dir / (name + "_meta.json"), meta);
}
