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
import gameplay;
import navigation;
import physics;
import ai;
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

    float horizontal_distance(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
    }

    // camera yaw / pitch (Rhea camera: looks -Z at yaw 0) looking along direction
    glm::vec2 view_angles(const glm::vec3& direction)
    {
        return { std::atan2(-direction.x, -direction.z), std::asin(std::clamp(direction.y, -1.0f, 1.0f)) };
    }

    glm::vec3 view_direction(float yaw, float pitch)
    {
        return { -std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch) };
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
    const bool is_playtest = route && (*route)["playtest"].isObject();
    if (!route || !(*route)["waypoints"].isArray() || (*route)["waypoints"].size() < (is_playtest ? 1u : 2u))
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

    if (is_playtest)
    {
        const Json::Value& test = (*route)["playtest"];
        playtest = true;
        playtest_seconds = test.get("seconds", playtest_seconds).asFloat();
        roam_radius = test.get("roam_radius", roam_radius).asFloat();
        hunt_range = test.get("hunt_range", hunt_range).asFloat();
        engage_range = test.get("engage_range", engage_range).asFloat();
        aim_turn_rate = glm::radians(test.get("turn_degrees", glm::degrees(aim_turn_rate)).asFloat());
        aim_tolerance = glm::radians(test.get("aim_tolerance_degrees", glm::degrees(aim_tolerance)).asFloat());
        weapon_seconds = std::max(test.get("weapon_seconds", weapon_seconds).asFloat(), 1.0f);
        heal_below = test.get("heal_below", heal_below).asFloat();
        defend_range = test.get("defend_range", defend_range).asFloat();
        fight_kills = std::max(test.get("fight_kills", fight_kills).asUInt(), 1u);
        fight_seconds = test.get("fight_seconds", fight_seconds).asFloat();
        roam_seconds = test.get("roam_seconds", roam_seconds).asFloat();
        search_seconds = test.get("search_seconds", search_seconds).asFloat();
        scan_interval = std::max(test.get("scan_interval", scan_interval).asFloat(), 3.0f);
        for (const Json::Value& box : test["avoid"])
            if (box.isArray() && box.size() == 4)
                avoid.push_back({ box[0].asFloat(), box[1].asFloat(), box[2].asFloat(), box[3].asFloat() });
        for (const Json::Value& weapon : test["weapons"])
            if (weapons::find(weapon.asString()))
                playtest_weapons.push_back(weapon.asString());
        if (playtest_weapons.empty())
            playtest_weapons = weapons::get_loadout();
        for (const Json::Value& line : test["console_after"])
            console_after.push_back(line.asString());
        for (const Json::Value& line : test["console"])
            cvar::execute(line.asString());
        rng.seed(test.get("seed", 1).asUInt());
    }
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

    const glm::vec2 first_leg = waypoints.size() > 1 ? waypoints[1] - waypoints[0] : glm::vec2(0.0f, -1.0f);
    travel_yaw = std::atan2(-first_leg.x, -first_leg.y);
    next_waypoint = waypoints.size() > 1 ? 1 : 0;
    start_time = world.get_time_seconds();
    state = State::warmup;

    if (playtest)
        LogBenchmark.Log("Playtest '%s': %.0f s, %zu roam points, %zu weapons, warmup %.1f s", route_path.c_str(), playtest_seconds,
            waypoints.size(), playtest_weapons.size(), warmup_seconds);
    else
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

    if (playtest)
        steer_playtest(world, character, yaw, pitch);
    else
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

bool Benchmark::avoided(const glm::vec3& point) const
{
    return std::ranges::any_of(avoid, [&] (const glm::vec4& box) {
        return point.x >= box.x && point.x <= box.z && point.z >= box.y && point.z <= box.w;
    });
}

bool Benchmark::path_avoided(const glm::vec3& from, const std::vector<glm::vec3>& corners) const
{
    // segment against box on the xz plane (slabs)
    auto crosses = [] (glm::vec2 a, glm::vec2 b, const glm::vec4& box) {
        float t0 = 0.0f, t1 = 1.0f;
        const glm::vec2 d = b - a;
        const glm::vec2 lo(box.x, box.y), hi(box.z, box.w);
        for (int axis = 0; axis < 2; ++axis)
        {
            if (std::abs(d[axis]) < 1e-6f)
            {
                if (a[axis] < lo[axis] || a[axis] > hi[axis])
                    return false;
                continue;
            }
            float near_t = (lo[axis] - a[axis]) / d[axis], far_t = (hi[axis] - a[axis]) / d[axis];
            if (near_t > far_t)
                std::swap(near_t, far_t);
            t0 = std::max(t0, near_t);
            t1 = std::min(t1, far_t);
            if (t0 > t1)
                return false;
        }
        return true;
    };
    glm::vec2 previous(from.x, from.z);
    for (const glm::vec3& corner : corners)
    {
        const glm::vec2 next(corner.x, corner.z);
        for (const glm::vec4& box : avoid)
            if (crosses(previous, next, box))
                return true;
        previous = next;
    }
    return false;
}

void Benchmark::set_phase(uint32_t next, double now)
{
    static constexpr const char* names[] = { "search", "fight", "roam" };
    if (next != phase)
        LogBenchmark.Log("Playtest %.0f s: %s -> %s (%u kills)", now - start_time, names[phase], names[next], kills);
    phase = next;
    phase_start = now;
    phase_kills = kills;
    last_contact = now;
    if (next == Phase::roam)
        destination.reset();
}

void Benchmark::pick_destination(World& world, const glm::vec3& feet)
{
    const nav::NavMesh* navmesh = world.registry.find_resource<nav::NavMesh>();
    destination.reset();
    // a random spot around a roam point the path to which keeps out of the avoided boxes
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const glm::vec2 point = waypoints[std::uniform_int_distribution<size_t>(0, waypoints.size() - 1)(rng)];
        glm::vec3 spot(point.x, feet.y, point.y);
        if (navmesh)
            if (std::optional<glm::vec3> on_mesh = navmesh->project(spot, { 2.0f, 60.0f, 2.0f }))
            {
                spot = *on_mesh;
                if (std::optional<glm::vec3> around = navmesh->random_point_around(*on_mesh, roam_radius, uint32_t(rng())))
                    spot = *around;
            }
        if (horizontal_distance(spot, feet) < 8.0f || avoided(spot))
            continue;
        destination = spot;
        std::vector<glm::vec3> corners;
        if (!navmesh || !navmesh->find_path(feet, spot, corners) || !path_avoided(feet, corners))
            break;
    }
    if (!destination)
        destination = glm::vec3(waypoints[0].x, feet.y, waypoints[0].y);
    hunting = false;
    ++destinations;
    next_repath = 0.0;
}

void Benchmark::steer_playtest(World& world, ecs::Entity character, float& yaw, float& pitch)
{
    constexpr glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    ecs::Registry& registry = world.registry;
    const loco::LocomotionCharacter& movement = *registry.get<loco::LocomotionCharacter>(character);
    const double now = world.get_time_seconds();
    const float dt = float(std::min(world.get_delta_seconds(), 0.1));
    const glm::vec3 feet = movement.position;
    WeaponHolder* holder = registry.get<WeaponHolder>(character);
    const nav::NavMesh* navmesh = registry.find_resource<nav::NavMesh>();

    if (now - start_time >= playtest_seconds)
    {
        loco::set_scripted_locomotion_input(loco::LocomotionInput{});
        weapons::set_scripted_trigger(false);
        if (holder && saved_infinite_ammo)
            holder->infinite_ammo = *saved_infinite_ammo;
        state = State::draining;
        drain_frames = 0;
        return;
    }
    lap = uint32_t((now - start_time) / 60.0);
    if (phase_start == 0.0)
        set_phase(Phase::search, now);

    // ---- the player: endless ammunition, the weapons of the loadout in turn, healed before the jackals finish it
    if (holder && !saved_infinite_ammo)
    {
        saved_infinite_ammo = holder->infinite_ammo;
        holder->infinite_ammo = true;
    }
    if (!playtest_weapons.empty() && now >= next_weapon_time)
    {
        if (next_weapon_time > 0.0)
            weapon_index = (weapon_index + 1) % uint32_t(playtest_weapons.size());
        next_weapon_time = now + weapon_seconds;
        cvar::execute(std::format("weapons.select {}", playtest_weapons[weapon_index]));
        LogBenchmark.Log("Playtest %.0f s: weapon %s", now - start_time, playtest_weapons[weapon_index].c_str());
    }
    if (Health* health = registry.get<Health>(character); health && !registry.has<Dead>(character)
        && health->current < health->max * heal_below)
    {
        health->current = health->max;
        ++heals;
    }

    // ---- the jackals: kills since the last frame, the closest one in sight to shoot, the closest one to hunt
    for (ecs::Entity e : live_jackals)
        if (registry.alive(e) && registry.has<Dead>(e))
            ++kills;
    live_jackals.clear();

    const glm::vec3 eye = loco::make_locomotion_camera(world, character, yaw, pitch).position.glm();
    struct Seen
    {
        ecs::Entity entity;
        glm::vec3 feet;
        glm::vec3 center;
        glm::vec3 velocity;
        float distance;
    };
    std::vector<Seen> jackals;
    ecs::Query<const ai::Perceivable, ecs::With<Jackal>>(registry).each([&] (ecs::Entity e, const ai::Perceivable& p) {
        if (registry.has<Dead>(e) || !p.enabled || !p.measured)
            return;
        live_jackals.push_back(e);
        const glm::vec3 center = p.position + up * p.center_height;
        jackals.push_back({ e, p.position, center, p.velocity, glm::length(center - eye) });
    });
    jackals_alive = uint32_t(jackals.size());

    phys::PhysicsScene& physics = world.get_physics();
    phys::QueryFilter sight;
    sight.categories = phys::Category::static_world | phys::Category::voxel;
    sight.hit_back_faces = true;
    auto in_sight = [&] (const glm::vec3& point) {
        const glm::vec3 to = point - eye;
        const float length = glm::length(to);
        if (length < 1e-3f)
            return true;
        const std::optional<phys::Hit> hit = physics.raycast(eye, to / length, length, sight);
        return !hit || hit->distance > length - 0.3f;
    };

    // roaming: only what comes close is shot, nothing is hunted
    const float shoot_range = phase == Phase::roam ? defend_range : engage_range;
    const Seen* current = nullptr;
    const Seen* prey = nullptr;
    for (const Seen& j : jackals)
    {
        if (j.entity == target && j.distance <= shoot_range * 1.2f)
            current = &j;
        if (phase != Phase::roam && j.distance <= hunt_range && !avoided(j.feet) && (!prey || j.distance < prey->distance))
            prey = &j;
    }
    if (now >= next_target_search)
    {
        next_target_search = now + 0.2;
        const Seen* best = nullptr;
        float best_score = std::numeric_limits<float>::infinity();
        for (const Seen& j : jackals)
        {
            if (j.distance > shoot_range)
                continue;
            const float score = j.distance - (j.entity == target ? 4.0f : 0.0f);
            if (score < best_score && in_sight(j.center))
            {
                best = &j;
                best_score = score;
            }
        }
        current = best;
    }
    target = current ? current->entity : ecs::Entity{};
    engaged = current != nullptr;

    // ---- phases
    if (current || (phase == Phase::fight && prey))
        last_contact = now;
    const double in_phase = now - phase_start;
    if (phase == Phase::search && current)
        set_phase(Phase::fight, now);
    else if (phase == Phase::search && in_phase > search_seconds)
        set_phase(Phase::roam, now);
    else if (phase == Phase::fight && (kills - phase_kills >= fight_kills || in_phase > fight_seconds || now - last_contact > 5.0))
        set_phase(Phase::roam, now);
    else if (phase == Phase::roam && in_phase > roam_seconds)
        set_phase(Phase::search, now);
    if (phase == Phase::roam)
        prey = nullptr;

    // ---- aim and fire
    const WeaponDef* def = holder ? weapons::find(holder->current) : nullptr;
    bool on_target = false;
    target_distance = 0.0f;
    if (current)
    {
        glm::vec3 point = current->center;
        if (def && def->mode == WeaponMode::Projectile && def->projectile_speed > 0.0f)
            point += current->velocity * (current->distance / def->projectile_speed);
        const glm::vec3 to = glm::normalize(point - eye);
        const glm::vec2 want = view_angles(to);
        yaw = wrap_angle(yaw + std::clamp(wrap_angle(want.x - yaw), -aim_turn_rate * dt, aim_turn_rate * dt));
        pitch += std::clamp(want.y - pitch, -aim_turn_rate * dt, aim_turn_rate * dt);
        const float error = std::acos(std::clamp(glm::dot(view_direction(yaw, pitch), to), -1.0f, 1.0f));
        on_target = error <= std::max(aim_tolerance, std::atan(0.2f / std::max(current->distance, 0.1f)));
        target_distance = current->distance;
        travel_yaw = yaw;
    }

    bool next_trigger = false;
    if (def)
    {
        const bool want_fire = on_target && current->distance <= def->range * 0.9f;
        if (def->automatic || def->mode == WeaponMode::Beam)
            next_trigger = want_fire;
        else
        {
            // one press per shot (a charge: held for the full charge, released to fire)
            const bool charge = def->mode == WeaponMode::Charge;
            const double hold = charge ? std::max(def->charge_time, def->min_charge) + 0.05 : 0.06;
            const double wait = charge ? 0.15 : std::max(def->cooldown, 0.05f) + 0.03;
            next_trigger = trigger ? now - trigger_time < hold : want_fire && now - trigger_time >= wait;
        }
    }
    if (next_trigger != trigger)
    {
        trigger = next_trigger;
        trigger_time = now;
    }
    weapons::set_scripted_trigger(trigger);

    // ---- where to go: the closest jackal (not through the avoided boxes), else a spot around a roam point
    if (prey && !hunting)
    {
        hunting = true;
        next_repath = 0.0;
    }
    else if (hunting && !prey)
        pick_destination(world, feet);
    else if (!destination || (!hunting && horizontal_distance(feet, *destination) < 2.0f))
        pick_destination(world, feet);

    if (now >= next_repath)
    {
        if (hunting)
            destination = prey->feet;
        path.clear();
        path_corner = 0;
        if (!navmesh || !navmesh->find_path(feet, *destination, path) || path.empty())
            path = { *destination };
        if (path_avoided(feet, path))
        {
            // the way leads through an avoided box: somewhere else
            pick_destination(world, feet);
            path.clear();
            if (!navmesh || !navmesh->find_path(feet, *destination, path) || path.empty())
                path = { *destination };
        }
        next_repath = now + 1.5;
    }
    while (path_corner + 1 < path.size() && horizontal_distance(feet, path[path_corner]) < 0.7f)
        ++path_corner;

    glm::vec2 move(0.0f);
    const glm::vec2 to_corner(path[path_corner].x - feet.x, path[path_corner].z - feet.z);
    if (glm::length(to_corner) > 0.3f)
        move = glm::normalize(to_corner);
    if (current && current->distance < 5.0f)
    {
        // too close: backs off and circles
        const glm::vec2 away = glm::normalize(glm::vec2(feet.x - current->feet.x, feet.z - current->feet.z) + glm::vec2(1e-4f));
        const glm::vec2 side(-away.y, away.x);
        move = glm::normalize(away + side * std::sin(float(now - start_time) * 0.8f));
    }
    else if (hunting && prey && prey->distance < 4.0f)
        move = glm::vec2(0.0f);   // close enough to the one it hunts: turns to it

    // stuck (a wall the path does not know of, a jackal in the way): another destination, the second time a teleport
    if (horizontal_distance(feet, progress_position) > 1.0f || glm::length(move) < 0.1f)
    {
        progress_position = feet;
        progress_time = now;
    }
    else if (now - progress_time > 4.0)
    {
        progress_time = now;
        if (++stuck_count >= 2)
        {
            stuck_count = 0;
            ++teleports;
            const glm::vec3 to = path[path_corner];
            LogBenchmark.Log("Playtest: stuck at (%.1f, %.1f, %.1f), teleporting to (%.1f, %.1f, %.1f)", feet.x, feet.y, feet.z,
                to.x, to.y, to.z);
            cvar::execute(std::format("player.teleport {} {} {}", to.x, to.y + 1.0f, to.z));
        }
        pick_destination(world, feet);
    }

    // ---- the camera: on the target, else along the way and around it; searching, a full look around now and then
    if (!current)
    {
        if (glm::length(move) > 0.1f)
        {
            const float move_yaw = std::atan2(-move.x, -move.y);
            travel_yaw = wrap_angle(travel_yaw + std::clamp(wrap_angle(move_yaw - travel_yaw), -2.5f * dt, 2.5f * dt));
        }
        const float t = float(now - start_time);
        float want_yaw = travel_yaw + sweep * std::sin(2.0f * PI * t / sweep_period);
        constexpr double scan_seconds = 2.5;
        if (phase == Phase::search && !prey && now - scan_start > scan_interval)
            scan_start = now;
        float turn_rate = 3.0f;
        if (now - scan_start < scan_seconds)
        {
            want_yaw = travel_yaw + 2.0f * PI * float((now - scan_start) / scan_seconds);
            turn_rate = 2.0f * PI / float(scan_seconds) + 1.0f;
        }
        const float want_pitch = base_pitch + pitch_sweep * std::sin(2.0f * PI * t / pitch_period);
        yaw = wrap_angle(yaw + std::clamp(wrap_angle(want_yaw - yaw), -turn_rate * dt, turn_rate * dt));
        pitch += std::clamp(want_pitch - pitch, -1.5f * dt, 1.5f * dt);
    }

    // the input is relative to the camera
    const glm::vec2 camera_forward(-std::sin(yaw), -std::cos(yaw));
    const glm::vec2 camera_right(std::cos(yaw), -std::sin(yaw));
    loco::LocomotionInput input;
    input.move_axis = glm::vec2(glm::dot(move, camera_right), glm::dot(move, camera_forward));
    input.sprint = !engaged && glm::length(move) > 0.5f;
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
    if (playtest)
    {
        frame.phase = phase;
        frame.jackals = jackals_alive;
        frame.engaged = engaged;
        frame.firing = trigger;
        frame.kills = kills;
        if (const Health* health = world.registry.get<Health>(character))
            frame.health = health->current;
        frame.weapon = weapon_index;
        frame.target_distance = target_distance;
    }
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
    if (playtest)
        weapons::set_scripted_trigger(std::nullopt);
    prof::set_is_profiling(false);
    gpuprof::set_enabled(false);

    write_results();

    double total_ms = 0.0;
    for (const Frame& frame : frames)
        total_ms += frame.frame_ms;
    LogBenchmark.Log("Benchmark '%s' finished: %zu frames in %.1f s (%.1f FPS on average), %u teleports -> cache/bench/%s_*",
        route_path.c_str(), frames.size(), total_ms * 1e-3, frames.empty() ? 0.0 : 1000.0 * double(frames.size()) / total_ms,
        teleports, name.c_str());
    if (playtest)
    {
        const size_t engaged_frames = std::ranges::count_if(frames, [] (const Frame& f) { return f.engaged; });
        LogBenchmark.Log("Playtest: %u jackals killed, %u heals, %u destinations, engaged %.0f%% of the frames", kills, heals,
            destinations, frames.empty() ? 0.0 : 100.0 * double(engaged_frames) / double(frames.size()));
        for (const std::string& line : console_after)
            cvar::execute(line);
    }

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
        file << "frame,time,wall,frame_ms,x,y,z,yaw,pitch,lap,waypoint,gpu_frame,phase,jackals,engaged,firing,kills,health,weapon,target_distance";
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
            file << std::format(",{},{},{},{},{},{:.1f},{},{:.2f}", frame.phase, frame.jackals, int(frame.engaged), int(frame.firing), frame.kills,
                frame.health, frame.weapon, frame.target_distance);
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
    if (playtest)
    {
        Json::Value test(Json::objectValue);
        test["seconds"] = playtest_seconds;
        test["kills"] = kills;
        test["heals"] = heals;
        test["destinations"] = destinations;
        Json::Value names(Json::arrayValue);
        for (const std::string& weapon : playtest_weapons)
            names.append(weapon);
        test["weapons"] = names;
        meta["playtest"] = test;
    }
    json_utils::save_json_to_path(dir / (name + "_meta.json"), meta);
}
