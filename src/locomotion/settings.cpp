module;

#include <json/value.h>

module locomotion;

import std.compat;
import glm;
import json_utils;
import paths;
import log;
import fixed_string;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogLocomotionSettings, Log);

namespace loco
{
    const char* to_string(Gait v)
    {
        switch (v) { case Gait::walking: return "walking"; case Gait::running: return "running"; case Gait::sprinting: return "sprinting"; }
        return "?";
    }
    const char* to_string(Stance v)
    {
        return v == Stance::standing ? "standing" : "crouching";
    }
    const char* to_string(RotationMode v)
    {
        switch (v) { case RotationMode::velocity_direction: return "velocity direction"; case RotationMode::view_direction: return "view direction"; case RotationMode::aiming: return "aiming"; }
        return "?";
    }
    const char* to_string(LocomotionMode v)
    {
        switch (v) { case LocomotionMode::none: return "none"; case LocomotionMode::grounded: return "grounded"; case LocomotionMode::in_air: return "in air"; }
        return "?";
    }
    const char* to_string(LocomotionAction v)
    {
        switch (v)
        {
        case LocomotionAction::none: return "none";
        case LocomotionAction::mantling: return "mantling";
        case LocomotionAction::rolling: return "rolling";
        case LocomotionAction::ragdolling: return "ragdolling";
        case LocomotionAction::getting_up: return "getting up";
        }
        return "?";
    }

    glm::vec3 SampledCurve::evaluate(float t) const
    {
        if (values.empty())
            return glm::vec3(0.0f);
        if (values.size() == 1 || t <= times.front())
            return values.front();
        if (t >= times.back())
            return values.back();
        const auto it = std::upper_bound(times.begin(), times.end(), t);
        const size_t i = (size_t)std::distance(times.begin(), it) - 1;
        const float span = times[i + 1] - times[i];
        return glm::mix(values[i], values[i + 1], span > 0.0f ? (t - times[i]) / span : 0.0f);
    }

    namespace
    {
        std::unordered_map<std::string, std::shared_ptr<const LocomotionSettings>> settings_cache;

        int rotation_mode_index(const std::string& name)
        {
            if (name == "velocity_direction") return (int)RotationMode::velocity_direction;
            if (name == "view_direction") return (int)RotationMode::view_direction;
            if (name == "aiming") return (int)RotationMode::aiming;
            return -1;
        }
    }

    std::shared_ptr<const LocomotionSettings> load_locomotion_settings(const std::string& rel_path)
    {
        if (auto it = settings_cache.find(rel_path); it != settings_cache.end())
            return it->second;
        if (!std::filesystem::exists(paths::get_assets_path() / rel_path))
        {
            LogLocomotionSettings.Log("Locomotion settings %s not found", rel_path.c_str());
            return nullptr;
        }
        std::optional<Json::Value> root = json_utils::load_json_asset(rel_path);
        if (!root)
            return nullptr;

        auto s = std::make_shared<LocomotionSettings>();
        s->path = rel_path;
        s->reference_pelvis_height = (*root).get("reference_pelvis_height", 0.9675).asFloat();

        // curves first: gaits point at them
        const Json::Value& curves = (*root)["curves"];
        for (const std::string& name : curves.getMemberNames())
        {
            SampledCurve curve;
            for (const Json::Value& t : curves[name]["times"])
                curve.times.push_back(t.asFloat());
            for (const Json::Value& v : curves[name]["values"])
                curve.values.push_back(v.isArray() ? glm::vec3(v[0].asFloat(), v[1].asFloat(), v[2].asFloat()) : glm::vec3(v.asFloat(), 0.0f, 0.0f));
            s->curves.emplace(name, std::move(curve));
        }
        auto find_curve = [&](const Json::Value& name) -> const SampledCurve* {
            auto it = s->curves.find(name.asString());
            return it != s->curves.end() ? &it->second : nullptr;
        };

        const Json::Value& c = (*root)["character"];
        s->moving_speed_threshold = c.get("moving_speed_threshold", 0.5).asFloat();
        const std::string in_air = c.get("in_air_rotation_mode", "rotate_to_velocity").asString();
        s->in_air_rotation_mode = in_air == "keep_view_space_rotation" ? InAirRotationMode::keep_view_space_rotation
            : in_air == "keep_world_rotation" ? InAirRotationMode::keep_world_rotation : InAirRotationMode::rotate_to_velocity;
        s->allow_aiming_when_in_air = c.get("allow_aiming_when_in_air", true).asBool();
        s->sprint_has_priority_over_aiming = c.get("sprint_has_priority_over_aiming", false).asBool();
        s->rotate_to_velocity_when_sprinting = c.get("rotate_to_velocity_when_sprinting", false).asBool();
        s->rotate_towards_desired_velocity = c.get("rotate_towards_desired_velocity_in_velocity_direction_rotation_mode", true).asBool();
        s->auto_rotate_on_any_input_while_not_moving = c.get("auto_rotate_on_any_input_while_not_moving_in_view_direction_rotation_mode", true).asBool();
        s->aiming_yaw_angle_limit = c.get("aiming_yaw_angle_limit", 70.0).asFloat();

        const Json::Value& m = (*root)["movement"];
        if (m["velocity_angle_to_speed_range"].isArray())
            s->velocity_angle_to_speed_range = glm::vec2(m["velocity_angle_to_speed_range"][0].asFloat(), m["velocity_angle_to_speed_range"][1].asFloat());
        const Json::Value& gaits = m["gaits"];
        for (const std::string& mode_name : gaits.getMemberNames())
        {
            const int mode = rotation_mode_index(mode_name);
            if (mode < 0)
                continue;
            for (const std::string& stance_name : gaits[mode_name].getMemberNames())
            {
                const int stance = stance_name == "crouching" ? 1 : 0;
                const Json::Value& g = gaits[mode_name][stance_name];
                GaitSettings& out = s->gaits[mode][stance];
                out.walk_forward_speed = g.get("walk_forward_speed", 1.75).asFloat();
                out.walk_backward_speed = g.get("walk_backward_speed", 1.75).asFloat();
                out.run_forward_speed = g.get("run_forward_speed", 3.75).asFloat();
                out.run_backward_speed = g.get("run_backward_speed", 3.75).asFloat();
                out.sprint_speed = g.get("sprint_speed", 6.5).asFloat();
                out.direction_dependent_speed = g.get("direction_dependent_speed", false).asBool();
                out.acceleration_deceleration_friction = find_curve(g["acceleration_deceleration_friction_curve"]);
                out.rotation_half_life = find_curve(g["rotation_half_life_curve"]);
            }
        }

        const Json::Value& p = (*root)["physics"];
        s->capsule_radius = p.get("capsule_radius", 0.3).asFloat();
        s->standing_height = p.get("standing_height", 1.8).asFloat();
        s->crouched_height = p.get("crouched_height", 1.12).asFloat();
        s->jump_velocity = p.get("jump_velocity", 4.2).asFloat();
        s->gravity = p.get("gravity", 9.8).asFloat();
        s->air_control = p.get("air_control", 0.15).asFloat();
        s->air_acceleration = p.get("air_acceleration", 20.0).asFloat();

        const Json::Value& a = (*root)["animation"];
        AnimationSettings& an = s->animation;
        auto f = [&](const char* key, float& v) { v = a.get(key, v).asFloat(); };
        auto v2 = [&](const char* key, glm::vec2& v) { if (a[key].isArray()) v = glm::vec2(a[key][0].asFloat(), a[key][1].asFloat()); };
        auto curve = [&](const char* key) { return find_curve(a[key]); };
        f("moving_smooth_speed_threshold", an.moving_smooth_speed_threshold);
        f("lean_interpolation_half_life", an.lean_interpolation_half_life);
        f("velocity_blend_interpolation_half_life", an.velocity_blend_interpolation_half_life);
        an.rotation_yaw_offset_forward = curve("rotation_yaw_offset_forward_curve");
        an.rotation_yaw_offset_backward = curve("rotation_yaw_offset_backward_curve");
        an.rotation_yaw_offset_left = curve("rotation_yaw_offset_left_curve");
        an.rotation_yaw_offset_right = curve("rotation_yaw_offset_right_curve");
        f("animated_walk_speed", an.animated_walk_speed);
        f("animated_run_speed", an.animated_run_speed);
        f("animated_sprint_speed", an.animated_sprint_speed);
        an.stride_blend_walk = curve("stride_blend_amount_walk_curve");
        an.stride_blend_run = curve("stride_blend_amount_run_curve");
        f("pivot_activation_speed_threshold", an.pivot_activation_speed_threshold);
        f("animated_crouch_speed", an.animated_crouch_speed);
        an.crouching_stride_blend = curve("crouching_stride_blend_amount_curve");
        an.in_air_lean_amount = curve("in_air_lean_amount_curve");
        an.ground_prediction_amount = curve("ground_prediction_amount_curve");
        f("quick_stop_blend_in", an.quick_stop_blend_in);
        f("quick_stop_blend_out", an.quick_stop_blend_out);
        v2("quick_stop_play_rate", an.quick_stop_play_rate);
        f("quick_stop_start_time", an.quick_stop_start_time);
        f("dynamic_transition_foot_lock_distance_threshold", an.dynamic_transition_foot_lock_distance_threshold);
        f("dynamic_transition_blend_duration", an.dynamic_transition_blend_duration);
        f("dynamic_transition_play_rate", an.dynamic_transition_play_rate);
        f("rotate_in_place_view_yaw_angle_threshold", an.rotate_in_place_view_yaw_angle_threshold);
        f("rotate_in_place_first_person_view_yaw_angle_threshold", an.rotate_in_place_first_person_view_yaw_angle_threshold);
        v2("rotate_in_place_reference_view_yaw_speed", an.rotate_in_place_reference_view_yaw_speed);
        v2("rotate_in_place_play_rate", an.rotate_in_place_play_rate);
        f("turn_in_place_view_yaw_angle_threshold", an.turn_in_place_view_yaw_angle_threshold);
        f("turn_in_place_view_yaw_speed_threshold", an.turn_in_place_view_yaw_speed_threshold);
        v2("turn_in_place_view_yaw_angle_to_activation_delay", an.turn_in_place_view_yaw_angle_to_activation_delay);
        f("turn_in_place_turn_180_angle_threshold", an.turn_in_place_turn_180_angle_threshold);
        f("turn_in_place_blend_duration", an.turn_in_place_blend_duration);
        f("turn_in_place_play_rate", an.turn_in_place_play_rate);
        an.stance_change_curve = curve("stance_change_curve");
        an.allow_foot_lock = a.get("allow_foot_lock", an.allow_foot_lock).asBool();
        f("foot_lock_thigh_angle_limit", an.foot_lock_thigh_angle_limit);
        f("foot_lock_foot_angle_limit", an.foot_lock_foot_angle_limit);
        f("head_pitch_angle_half_life", an.head_pitch_angle_half_life);
        f("head_yaw_angle_half_life", an.head_yaw_angle_half_life);
        f("head_switch_look_sides_yaw_angle_half_life", an.head_switch_look_sides_yaw_angle_half_life);
        f("head_first_person_pitch_angle_half_life", an.head_first_person_pitch_angle_half_life);
        f("head_first_person_yaw_angle_half_life", an.head_first_person_yaw_angle_half_life);

        const Json::Value& r = (*root)["rolling"];
        RollingSettings& ro = s->rolling;
        ro.montage = r.get("montage", "animations/als/Actions/Roll/AM_Als_Roll.json").asString();
        ro.play_rate = r.get("play_rate", ro.play_rate).asFloat();
        ro.crouch_on_start = r.get("crouch_on_start", ro.crouch_on_start).asBool();
        ro.rotate_to_input_on_start = r.get("rotate_to_input_on_start", ro.rotate_to_input_on_start).asBool();
        ro.rotation_half_life = r.get("rotation_half_life", ro.rotation_half_life).asFloat();
        ro.start_on_land = r.get("start_on_land", ro.start_on_land).asBool();
        ro.on_land_speed_threshold = r.get("on_land_speed_threshold", ro.on_land_speed_threshold).asFloat();
        ro.interrupt_when_in_air = r.get("interrupt_when_in_air", ro.interrupt_when_in_air).asBool();

        auto vec2 = [](const Json::Value& v, glm::vec2 fallback) {
            return v.isArray() && v.size() == 2 ? glm::vec2(v[0].asFloat(), v[1].asFloat()) : fallback;
        };
        const Json::Value& m2 = (*root)["mantling"];
        MantlingSettings& ma = s->mantling;
        ma.allow = m2.get("allow", ma.allow).asBool();
        ma.auto_start_in_air = m2.get("auto_start_in_air", ma.auto_start_in_air).asBool();
        ma.trace_angle_threshold = m2.get("trace_angle_threshold", ma.trace_angle_threshold).asFloat();
        ma.max_reach_angle = m2.get("max_reach_angle", ma.max_reach_angle).asFloat();
        ma.slope_angle_threshold = m2.get("slope_angle_threshold", ma.slope_angle_threshold).asFloat();
        ma.high_height_threshold = m2.get("high_height_threshold", ma.high_height_threshold).asFloat();
        ma.blend_out_duration = m2.get("blend_out_duration", ma.blend_out_duration).asFloat();
        auto trace = [&](const Json::Value& t, MantlingTraceSettings& out) {
            out.ledge_height = vec2(t["ledge_height"], out.ledge_height);
            out.reach_distance = t.get("reach_distance", out.reach_distance).asFloat();
            out.target_location_offset = t.get("target_location_offset", out.target_location_offset).asFloat();
            out.start_location_offset = t.get("start_location_offset", out.start_location_offset).asFloat();
        };
        trace(m2["grounded_trace"], ma.grounded_trace);
        trace(m2["in_air_trace"], ma.in_air_trace);
        const char* type_names[3] = { "high", "low", "in_air" };
        for (int i = 0; i < 3; ++i)
        {
            const Json::Value& t = m2["types"][type_names[i]];
            MantlingTypeSettings& out = ma.types[i];
            out.montage = t.get("montage", "").asString();
            out.start_time_reference_height = vec2(t["start_time_reference_height"], out.start_time_reference_height);
            out.start_time = vec2(t["start_time"], out.start_time);
            out.warp_time_range = vec2(t["warp_time_range"], out.warp_time_range);
        }

        settings_cache.emplace(rel_path, s);
        return s;
    }
}
