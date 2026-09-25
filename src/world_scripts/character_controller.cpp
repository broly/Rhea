module;

#include <json/value.h>

module character_controller;

import std.compat;
import assertions;
import fixed_string;

import json_utils;
import log;

#include "common/assertion_macros.h"
#include "logging/log_macro.h"

DEFINE_LOGGER(LogCharacterController, Log);


static constexpr float PI = 3.14159265358979f;

static float move_towards(float value, float target, float max_delta)
{
    if (std::abs(target - value) <= max_delta)
        return target;
    return value + (target > value ? max_delta : -max_delta);
}

static float wrap_angle(float a)
{
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

static glm::vec3 camera_forward(float yaw, float pitch)
{
    // matches math::from_euler_rotation(pitch, yaw, 0) applied to -Z
    return glm::vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
}


bool CharacterController::load_clip(const Json::Value& locomotion, const char* role, Clip& clip)
{
    if (!locomotion.isMember(role) || !locomotion[role].isObject())
    {
        LogCharacterController.Log("Locomotion: no '%s' clip", role);
        return false;
    }

    const Json::Value& entry = locomotion[role];

    clip.handle = AssetManager::get().load_animation(entry["clip"].asString());
    if (!clip.handle.is_valid())
        return false;

    const AnimationClip& anim = clip.handle.get();
    clip.binding = AnimationBinding::bind(anim, mesh_component->skeletal_mesh.get().skeleton);
    clip.duration = anim.duration;
    clip.ground_speed = entry["ground_speed"].asFloat();
    return true;
}

bool CharacterController::init(std::shared_ptr<RhActor> in_actor, const std::string& locomotion_json_path)
{
    actor = in_actor;
    if (!actor)
        return false;

    mesh_component = actor->find_component<RhComp_SkeletalMesh>();
    if (!mesh_component)
    {
        LogCharacterController.Log("Actor '%s' has no skeletal mesh", actor->name.to_string().c_str());
        return false;
    }

    std::optional<Json::Value> locomotion = json_utils::load_json_asset(locomotion_json_path);
    if (!locomotion.has_value())
        return false;

    const bool loaded =
        load_clip(*locomotion, "idle", idle) &
        load_clip(*locomotion, "walk", walk) &
        load_clip(*locomotion, "run", run) &
        load_clip(*locomotion, "jump_start", jump_start) &
        load_clip(*locomotion, "jump_loop", jump_loop) &
        load_clip(*locomotion, "jump_end", jump_end);

    if (!loaded || walk.ground_speed <= 0.0f || run.ground_speed <= walk.ground_speed)
    {
        LogCharacterController.Log("Locomotion set '%s' is incomplete", locomotion_json_path.c_str());
        return false;
    }

    bind_pose = make_bind_pose(mesh_component->skeletal_mesh.get().skeleton);

    const Transform t = mesh_component->get_transform();
    position = t.position.glm();
    const glm::vec3 facing = t.rotation.glm() * glm::vec3(0, 0, 1);
    heading = std::atan2(facing.x, facing.z);
    move_direction = glm::vec3(std::sin(heading), 0, std::cos(heading));

    LogCharacterController.Log("Character controller for '%s': walk %.2f m/s, run %.2f m/s",
        actor->name.to_string().c_str(), walk.ground_speed, run.ground_speed);
    return true;
}

void CharacterController::load_poses(const std::string& poses_json_path)
{
    std::optional<Json::Value> json = json_utils::load_json_asset(poses_json_path);
    if (!json.has_value() || !(*json)["poses"].isArray())
    {
        LogCharacterController.Log("No poses in '%s'", poses_json_path.c_str());
        return;
    }
    
    const Skeleton& skeleton = mesh_component->skeletal_mesh.get().skeleton;
    
    for (const Json::Value& entry : (*json)["poses"])
    {
        Clip clip;
        clip.handle = AssetManager::get().load_animation(entry["clip"].asString());
        if (!clip.handle.is_valid())
            continue;
        const AnimationClip& anim = clip.handle.get();
        clip.binding = AnimationBinding::bind(anim, skeleton);
        clip.duration = anim.duration;
        clip.name = entry["name"].asString();
        poses.push_back(std::move(clip));
    }
    LogCharacterController.Log("Loaded %zu poses", poses.size());
}

void CharacterController::select_pose(int32_t pose_index)
{
    if (pose_index >= (int32_t)poses.size())
        return;
    if (pose_index == active_pose)
        return;
    
    // the current pose fades out while the new one fades in
    if (active_pose >= 0)
    {
        fading_pose = active_pose;
        fading_pose_time = pose_time;
        fading_pose_weight = pose_weight;
    }
    
    active_pose = pose_index;
    pose_time = 0.0f;
    pose_weight = 0.0f;
    
    if (pose_index >= 0)
        LogCharacterController.Log("Pose %d/%zu: %s", pose_index + 1, poses.size(), poses[pose_index].name.c_str());
    else
        LogCharacterController.Log("Pose: locomotion");
}

void CharacterController::load_expressions(const std::string& expressions_json_path)
{
    std::optional<Json::Value> json = json_utils::load_json_asset(expressions_json_path);
    if (!json.has_value() || !(*json)["expressions"].isArray())
    {
        LogCharacterController.Log("No expressions in '%s'", expressions_json_path.c_str());
        return;
    }

    const SkeletalMesh& mesh = mesh_component->skeletal_mesh.get();
    if (mesh.num_morph_targets() == 0)
    {
        LogCharacterController.Log("Mesh '%s' has no morph targets, expressions are ignored", mesh.name.c_str());
        return;
    }

    for (const Json::Value& entry : (*json)["expressions"])
    {
        Expression expression;
        expression.name = entry["name"].asString();
        expression.weights.assign(mesh.num_morph_targets(), 0.0f);

        const Json::Value& weights = entry["weights"];
        for (const std::string& target_name : weights.getMemberNames())
        {
            std::optional<uint32_t> target = mesh.find_morph_target(target_name);
            if (!target)
            {
                LogCharacterController.Log("Expression '%s': unknown morph target '%s'",
                    expression.name.c_str(), target_name.c_str());
                continue;
            }
            expression.weights[*target] = std::clamp(weights[target_name].asFloat(), 0.0f, 1.0f);
        }
        expressions.push_back(std::move(expression));
    }

    expression_weights = mesh_component->get_morph_weights();
    expression_from = expression_weights;
    LogCharacterController.Log("Loaded %zu expressions", expressions.size());
}

void CharacterController::select_expression(int32_t expression_index)
{
    if (expression_index >= (int32_t)expressions.size() || expression_index == active_expression)
        return;

    // blend from wherever the face is now (also mid-blend)
    expression_from = expression_weights;
    expression_blend = 0.0f;
    active_expression = expression_index;

    LogCharacterController.Log("Expression: %s",
        expression_index >= 0 ? expressions[expression_index].name.c_str() : "neutral");
}

void CharacterController::update_expression(float dt)
{
    if (expression_weights.empty() || expression_blend >= 1.0f)
        return;

    expression_blend = move_towards(expression_blend, 1.0f, dt / expression_blend_time);
    const float t = expression_blend * expression_blend * (3.0f - 2.0f * expression_blend);   // smoothstep

    for (size_t i = 0; i < expression_weights.size(); ++i)
    {
        const float target = active_expression >= 0 ? expressions[active_expression].weights[i] : 0.0f;
        expression_weights[i] = glm::mix(expression_from[i], target, t);
    }
    mesh_component->set_morph_weights(expression_weights);
}

void CharacterController::tick(float dt, const Input* input, float camera_yaw)
{
    dt = std::min(dt, 0.1f);   // hitches (shader compilation, loading)
    update_movement(dt, input, camera_yaw);
    update_pose(dt);
    update_expression(dt);

    Transform t = mesh_component->get_transform();
    t.position = position;
    t.rotation = glm::angleAxis(heading, glm::vec3(0, 1, 0));
    actor->set_transform(t);
}

void CharacterController::update_movement(float dt, const Input* input, float camera_yaw)
{
    // ---- input, camera relative ----
    glm::vec2 axis(0.0f);
    bool walk_modifier = false;
    bool jump_down = false;

    if (input)
    {
        if (input->is_key_down(Key::W)) axis.y += 1.0f;
        if (input->is_key_down(Key::S)) axis.y -= 1.0f;
        if (input->is_key_down(Key::D)) axis.x += 1.0f;
        if (input->is_key_down(Key::A)) axis.x -= 1.0f;
        walk_modifier = input->is_key_down(Key::LeftShift);
        jump_down = input->is_key_down(Key::Space);
    }

    const glm::vec3 cam_forward = glm::vec3(-std::sin(camera_yaw), 0.0f, -std::cos(camera_yaw));
    const glm::vec3 cam_right = glm::vec3(std::cos(camera_yaw), 0.0f, -std::sin(camera_yaw));

    float target_speed = 0.0f;
    if (glm::length(axis) > 0.0f)
    {
        axis = glm::normalize(axis);
        move_direction = glm::normalize(cam_forward * axis.y + cam_right * axis.x);
        target_speed = walk_modifier ? walk.ground_speed : run.ground_speed;
        
        // moving leaves the pose
        if (active_pose >= 0)
            select_pose(-1);

        // turn towards the movement direction
        const float target_heading = std::atan2(move_direction.x, move_direction.z);
        const float delta = wrap_angle(target_heading - heading);
        heading = wrap_angle(heading + std::clamp(delta, -turn_rate * dt, turn_rate * dt));
    }

    // no steering in the air, keep momentum
    if (grounded)
        speed = move_towards(speed, target_speed, (target_speed > speed ? acceleration : deceleration) * dt);

    position += move_direction * speed * dt;

    // ---- jump ----
    if (jump_down && !jump_was_down && grounded)
    {
        grounded = false;
        vertical_velocity = jump_velocity;
        jump_state = JumpState::start;
        jump_time = 0.0f;
    }
    jump_was_down = jump_down;

    if (!grounded)
    {
        vertical_velocity -= gravity * dt;
        position.y += vertical_velocity * dt;
        if (position.y <= 0.0f)
        {
            position.y = 0.0f;
            vertical_velocity = 0.0f;
            grounded = true;
            jump_state = JumpState::land;
            jump_time = 0.0f;
        }
    }
}

void CharacterController::sample(const Clip& clip, float time, bool loop, BonePose& pose) const
{
    pose = bind_pose;
    sample_animation(clip.handle.get(), clip.binding, time, loop, pose);
}

void CharacterController::update_pose(float dt)
{
    // ---- locomotion: idle -> walk -> run ----
    idle_time += dt;

    if (speed <= walk.ground_speed)
    {
        const float w = speed / walk.ground_speed;
        locomotion_phase = std::fmod(locomotion_phase + dt / walk.duration, 1.0f);

        sample(idle, idle_time, true, pose_a);
        sample(walk, locomotion_phase * walk.duration, true, pose_b);
        blend_poses(pose_a, pose_b, w, pose_locomotion);
    }
    else
    {
        const float w = std::clamp((speed - walk.ground_speed) / (run.ground_speed - walk.ground_speed), 0.0f, 1.0f);
        const float cycle = glm::mix(walk.duration, run.duration, w);
        locomotion_phase = std::fmod(locomotion_phase + dt / cycle, 1.0f);

        sample(walk, locomotion_phase * walk.duration, true, pose_a);
        sample(run, locomotion_phase * run.duration, true, pose_b);
        blend_poses(pose_a, pose_b, w, pose_locomotion);
    }

    // ---- poses (override locomotion, crossfaded) ----
    if (fading_pose >= 0)
    {
        fading_pose_time += dt;
        fading_pose_weight = move_towards(fading_pose_weight, 0.0f, dt / pose_blend_time);
        if (fading_pose_weight <= 0.0f)
            fading_pose = -1;
        else
        {
            sample(poses[fading_pose], fading_pose_time, true, pose_a);
            blend_poses(pose_locomotion, pose_a, fading_pose_weight, pose_locomotion);
        }
    }
    if (active_pose >= 0)
    {
        pose_time += dt;
        pose_weight = move_towards(pose_weight, 1.0f, dt / pose_blend_time);
        sample(poses[active_pose], pose_time, true, pose_a);
        blend_poses(pose_locomotion, pose_a, pose_weight, pose_locomotion);
    }
    
    // ---- jump overlay ----
    jump_time += dt;

    const Clip* jump_clip = nullptr;
    bool jump_loop_clip = false;

    switch (jump_state)
    {
    case JumpState::start:
        jump_clip = &jump_start;
        if (jump_time >= jump_start.duration)
        {
            jump_state = JumpState::loop;
            jump_time = 0.0f;
            jump_clip = &jump_loop;
            jump_loop_clip = true;
        }
        break;
    case JumpState::loop:
        jump_clip = &jump_loop;
        jump_loop_clip = true;
        break;
    case JumpState::land:
        jump_clip = &jump_end;
        if (jump_time >= jump_end.duration)
        {
            jump_state = JumpState::none;
            jump_clip = nullptr;
        }
        break;
    case JumpState::none:
        break;
    }

    const float target_jump_weight = jump_clip ? 1.0f : 0.0f;
    jump_weight = move_towards(jump_weight, target_jump_weight, dt / jump_blend_time);

    if (jump_clip && jump_weight > 0.0f)
    {
        sample(*jump_clip, jump_time, jump_loop_clip, pose_a);
        blend_poses(pose_locomotion, pose_a, jump_weight, pose_final);
    }
    else
    {
        pose_final = pose_locomotion;
    }

    mesh_component->set_local_pose(pose_to_local_matrices(pose_final));
}

Transform CharacterController::make_camera_transform(float camera_yaw, float camera_pitch) const
{
    const glm::vec3 pivot = position + glm::vec3(0.0f, camera_pivot_height, 0.0f);
    glm::vec3 camera_position = pivot - camera_forward(camera_yaw, camera_pitch) * camera_distance;
    camera_position.y = std::max(camera_position.y, 0.2f);

    Transform t;
    t.position = camera_position;
    t.rotation = math::from_euler_rotation(glm::vec3(camera_pitch, camera_yaw, 0.0f));
    return t;
}
