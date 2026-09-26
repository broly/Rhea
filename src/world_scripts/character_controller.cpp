module;

#include <json/value.h>

module character_controller;

import std.compat;
import assertions;
import fixed_string;

import json_utils;
import log;
import name;

#include "common/assertion_macros.h"
#include "logging/log_macro.h"

DEFINE_LOGGER(LogCharacterController, Log);


namespace
{
    constexpr float PI = 3.14159265358979f;

    constexpr float camera_distance = 3.2f;
    constexpr float camera_pivot_height = 1.45f;
    constexpr float camera_probe_radius = 0.2f;

    float move_towards(float value, float target, float max_delta)
    {
        if (std::abs(target - value) <= max_delta)
            return target;
        return value + (target > value ? max_delta : -max_delta);
    }

    float wrap_angle(float a)
    {
        while (a > PI) a -= 2.0f * PI;
        while (a < -PI) a += 2.0f * PI;
        return a;
    }

    glm::vec3 camera_forward(float yaw, float pitch)
    {
        // matches math::from_euler_rotation(pitch, yaw, 0) applied to -Z
        return glm::vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
    }

    using Clip = CharacterAnimator::Clip;
    using JumpState = CharacterAnimator::JumpState;

    bool load_clip(const Json::Value& locomotion, const char* role, const Skeleton& skeleton, Clip& clip)
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
        clip.binding = AnimationBinding::bind(anim, skeleton);
        clip.duration = anim.duration;
        clip.ground_speed = entry["ground_speed"].asFloat();
        clip.name = role;
        return true;
    }

    // Shared state for the camera probe (one per world is enough)
    struct CharacterCameraProbe
    {
        phys::Shape shape;
    };

    // ------------------------------------------------------------------ systems

    void read_player_input(ecs::Query<CharacterInput, const PlayerControlled> characters, ecs::Res<Input> input)
    {
        characters.each([&] (CharacterInput& command, const PlayerControlled& player) {
            command = {};
            command.camera_yaw = player.camera_yaw;
            if (!player.enabled)
                return;
            if (input->is_key_down(Key::W)) command.move_axis.y += 1.0f;
            if (input->is_key_down(Key::S)) command.move_axis.y -= 1.0f;
            if (input->is_key_down(Key::D)) command.move_axis.x += 1.0f;
            if (input->is_key_down(Key::A)) command.move_axis.x -= 1.0f;
            command.walk = input->is_key_down(Key::LeftShift);
            command.jump = input->is_key_down(Key::Space);
        });
    }

    void move_character(CharacterMovement& m, const CharacterInput& input, float dt, phys::PhysicsScene& physics)
    {
        m.previous_position = m.position;
        m.previous_heading = m.heading;

        // ---- input, camera relative ----
        const glm::vec3 cam_forward = glm::vec3(-std::sin(input.camera_yaw), 0.0f, -std::cos(input.camera_yaw));
        const glm::vec3 cam_right = glm::vec3(std::cos(input.camera_yaw), 0.0f, -std::sin(input.camera_yaw));

        float target_speed = 0.0f;
        if (glm::length(input.move_axis) > 0.0f)
        {
            const glm::vec2 axis = glm::normalize(input.move_axis);
            m.move_direction = glm::normalize(cam_forward * axis.y + cam_right * axis.x);
            target_speed = input.walk ? m.walk_speed : m.run_speed;

            // turn towards the movement direction
            const float target_heading = std::atan2(m.move_direction.x, m.move_direction.z);
            const float delta = wrap_angle(target_heading - m.heading);
            m.heading = wrap_angle(m.heading + std::clamp(delta, -m.turn_rate * dt, m.turn_rate * dt));
        }

        // no steering in the air, keep momentum
        if (m.grounded)
            m.speed = move_towards(m.speed, target_speed, (target_speed > m.speed ? m.acceleration : m.deceleration) * dt);

        // ---- jump ----
        bool jumped = false;
        if (input.jump && !m.jump_was_down && m.grounded)
        {
            jumped = true;
            m.grounded = false;
            m.vertical_velocity = m.jump_velocity;
            ++m.jump_count;
        }
        m.jump_was_down = input.jump;

        // ---- physics ----
        const phys::CharacterState previous = physics.get_character_state(m.capsule);

        glm::vec3 velocity = m.move_direction * m.speed;
        if (m.grounded)
        {
            // follow the ground (moving platforms); a gravity step keeps the capsule pressed to it
            velocity += previous.ground_velocity;
            m.vertical_velocity = 0.0f;
            velocity.y -= m.gravity * dt;
        }
        else
        {
            if (!jumped)
                m.vertical_velocity -= m.gravity * dt;
            velocity.y += m.vertical_velocity;
        }

        const phys::CharacterState state = physics.move_character(m.capsule, velocity, dt);
        m.position = state.position;

        // blocked by walls: the animation follows the real speed (no running in place)
        const glm::vec2 horizontal(state.velocity.x - state.ground_velocity.x, state.velocity.z - state.ground_velocity.z);
        m.speed = std::min(m.speed, glm::length(horizontal));

        const bool on_ground = state.ground == phys::GroundState::on_ground;
        if (on_ground && !jumped && m.vertical_velocity <= 0.0f)
        {
            if (!m.grounded)
                ++m.land_count;
            m.grounded = true;
            m.air_time = 0.0f;
        }
        else
        {
            m.grounded = false;
            m.air_time += dt;
            // hit a ceiling / landed on something mid-jump
            m.vertical_velocity = state.velocity.y;
        }
    }

    void sample(const CharacterAnimator& a, const Clip& clip, float time, bool loop, BonePose& pose)
    {
        pose = a.bind_pose;
        sample_animation(clip.handle.get(), clip.binding, time, loop, pose);
    }

    void update_pose(CharacterAnimator& a, const CharacterMovement& m, const CharacterInput* input, float dt)
    {
        // moving leaves the pose
        if (input && glm::length(input->move_axis) > 0.0f && a.active_pose >= 0)
            a.select_pose(-1);

        // ---- jump / fall / land events of the movement ----
        if (m.jump_count != a.seen_jump_count)
        {
            a.seen_jump_count = m.jump_count;
            a.jump_state = JumpState::start;
            a.jump_time = 0.0f;
        }
        if (m.land_count != a.seen_land_count)
        {
            a.seen_land_count = m.land_count;
            if (a.jump_state != JumpState::none)
            {
                a.jump_state = JumpState::land;
                a.jump_time = 0.0f;
            }
        }
        // walked off a ledge
        if (a.jump_state == JumpState::none && !m.grounded && m.air_time > a.fall_animation_delay)
        {
            a.jump_state = JumpState::loop;
            a.jump_time = 0.0f;
        }

        // ---- locomotion: idle -> walk -> run ----
        a.idle_time += dt;

        if (m.speed <= a.walk.ground_speed)
        {
            const float w = m.speed / a.walk.ground_speed;
            a.locomotion_phase = std::fmod(a.locomotion_phase + dt / a.walk.duration, 1.0f);

            sample(a, a.idle, a.idle_time, true, a.pose_a);
            sample(a, a.walk, a.locomotion_phase * a.walk.duration, true, a.pose_b);
            blend_poses(a.pose_a, a.pose_b, w, a.pose_locomotion);
        }
        else
        {
            const float w = std::clamp((m.speed - a.walk.ground_speed) / (a.run.ground_speed - a.walk.ground_speed), 0.0f, 1.0f);
            const float cycle = glm::mix(a.walk.duration, a.run.duration, w);
            a.locomotion_phase = std::fmod(a.locomotion_phase + dt / cycle, 1.0f);

            sample(a, a.walk, a.locomotion_phase * a.walk.duration, true, a.pose_a);
            sample(a, a.run, a.locomotion_phase * a.run.duration, true, a.pose_b);
            blend_poses(a.pose_a, a.pose_b, w, a.pose_locomotion);
        }

        // ---- poses (override locomotion, crossfaded) ----
        if (a.fading_pose >= 0)
        {
            a.fading_pose_time += dt;
            a.fading_pose_weight = move_towards(a.fading_pose_weight, 0.0f, dt / a.pose_blend_time);
            if (a.fading_pose_weight <= 0.0f)
                a.fading_pose = -1;
            else
            {
                sample(a, a.poses[a.fading_pose], a.fading_pose_time, true, a.pose_a);
                blend_poses(a.pose_locomotion, a.pose_a, a.fading_pose_weight, a.pose_locomotion);
            }
        }
        if (a.active_pose >= 0)
        {
            a.pose_time += dt;
            a.pose_weight = move_towards(a.pose_weight, 1.0f, dt / a.pose_blend_time);
            sample(a, a.poses[a.active_pose], a.pose_time, true, a.pose_a);
            blend_poses(a.pose_locomotion, a.pose_a, a.pose_weight, a.pose_locomotion);
        }

        // ---- jump overlay ----
        a.jump_time += dt;

        const Clip* jump_clip = nullptr;
        bool jump_loop_clip = false;

        switch (a.jump_state)
        {
        case JumpState::start:
            jump_clip = &a.jump_start;
            if (a.jump_time >= a.jump_start.duration)
            {
                a.jump_state = JumpState::loop;
                a.jump_time = 0.0f;
                jump_clip = &a.jump_loop;
                jump_loop_clip = true;
            }
            break;
        case JumpState::loop:
            jump_clip = &a.jump_loop;
            jump_loop_clip = true;
            break;
        case JumpState::land:
            jump_clip = &a.jump_end;
            if (a.jump_time >= a.jump_end.duration)
            {
                a.jump_state = JumpState::none;
                jump_clip = nullptr;
            }
            break;
        case JumpState::none:
            break;
        }

        const float target_jump_weight = jump_clip ? 1.0f : 0.0f;
        a.jump_weight = move_towards(a.jump_weight, target_jump_weight, dt / a.jump_blend_time);

        if (jump_clip && a.jump_weight > 0.0f)
        {
            sample(a, *jump_clip, a.jump_time, jump_loop_clip, a.pose_a);
            blend_poses(a.pose_locomotion, a.pose_a, a.jump_weight, a.pose_final);
        }
        else
        {
            a.pose_final = a.pose_locomotion;
        }
    }

    void update_expression(CharacterAnimator& a, SkinnedMesh& mesh, float dt)
    {
        if (a.expression_weights.empty() || a.expression_blend >= 1.0f)
            return;

        a.expression_blend = move_towards(a.expression_blend, 1.0f, dt / a.expression_blend_time);
        const float t = a.expression_blend * a.expression_blend * (3.0f - 2.0f * a.expression_blend);   // smoothstep

        for (size_t i = 0; i < a.expression_weights.size(); ++i)
        {
            const float target = a.active_expression >= 0 ? a.expressions[a.active_expression].weights[i] : 0.0f;
            a.expression_weights[i] = glm::mix(a.expression_from[i], target, t);
        }
        mesh.set_morph_weights(a.expression_weights);
    }

    void move_characters(ecs::Query<CharacterMovement, const CharacterInput> characters,
        ecs::ResMut<phys::PhysicsScene> physics, ecs::Res<ecs::SimTime> time)
    {
        characters.each([&] (CharacterMovement& movement, const CharacterInput& command) {
            move_character(movement, command, (float)time->dt, *physics);
        });
    }

    // the probe shape is made once the physics scene exists (after the level is loaded)
    void create_camera_probe(ecs::Registry& registry, ecs::ResMut<phys::PhysicsScene> physics)
    {
        if (!registry.find_resource<CharacterCameraProbe>())
            registry.set_resource<CharacterCameraProbe>(physics->create_shape(phys::SphereShape{ camera_probe_radius }));
    }

    void animate_characters(ecs::Registry& registry)
    {
        const float dt = std::min((float)registry.resource<ecs::FrameTime>().dt, 0.1f);   // hitches (shader compilation, loading)
        ecs::Query<CharacterAnimator, SkinnedMesh, const CharacterMovement>(registry).each(
            [&] (ecs::Entity e, CharacterAnimator& animator, SkinnedMesh& mesh, const CharacterMovement& movement)
            {
                update_pose(animator, movement, registry.get<CharacterInput>(e), dt);
                mesh.set_local_pose(pose_to_local_matrices(animator.pose_final));
                update_expression(animator, mesh, dt);
            });
    }

    // Render transform between the last two simulated ticks
    void interpolate_characters(ecs::Query<Transform, const CharacterMovement> characters, ecs::Res<ecs::FrameTime> time)
    {
        const float alpha = (float)time->alpha;
        characters.each([&] (Transform& transform, const CharacterMovement& m) {
            transform.position = glm::mix(m.previous_position, m.position, alpha);
            const float heading = m.previous_heading + wrap_angle(m.heading - m.previous_heading) * alpha;
            transform.rotation = glm::angleAxis(heading, glm::vec3(0, 1, 0));
        });
    }
}


void CharacterAnimator::load_poses(const std::string& poses_json_path, const Skeleton& skeleton)
{
    std::optional<Json::Value> json = json_utils::load_json_asset(poses_json_path);
    if (!json.has_value() || !(*json)["poses"].isArray())
    {
        LogCharacterController.Log("No poses in '%s'", poses_json_path.c_str());
        return;
    }

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

void CharacterAnimator::select_pose(int32_t pose_index)
{
    if (pose_index >= (int32_t)poses.size() || pose_index == active_pose)
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

void CharacterAnimator::load_expressions(const std::string& expressions_json_path, const SkinnedMesh& skinned)
{
    std::optional<Json::Value> json = json_utils::load_json_asset(expressions_json_path);
    if (!json.has_value() || !(*json)["expressions"].isArray())
    {
        LogCharacterController.Log("No expressions in '%s'", expressions_json_path.c_str());
        return;
    }

    const SkeletalMesh& mesh = skinned.skeletal_mesh.get();
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

    expression_weights = skinned.get_morph_weights();
    expression_from = expression_weights;
    LogCharacterController.Log("Loaded %zu expressions", expressions.size());
}

void CharacterAnimator::select_expression(int32_t expression_index)
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

bool init_character(World& world, ecs::Entity e, const std::string& locomotion_json_path)
{
    ecs::Registry& registry = world.registry;
    const std::string name = scene::get_name(registry, e);

    const SkinnedMesh* skinned = registry.get<SkinnedMesh>(e);
    if (!skinned || !skinned->pose)
    {
        LogCharacterController.Log("Entity '%s' has no skinned mesh", name.c_str());
        return false;
    }
    const Skeleton& skeleton = skinned->get_skeleton();

    std::optional<Json::Value> locomotion = json_utils::load_json_asset(locomotion_json_path);
    if (!locomotion.has_value())
        return false;

    CharacterAnimator animator;
    const bool loaded =
        load_clip(*locomotion, "idle", skeleton, animator.idle) &
        load_clip(*locomotion, "walk", skeleton, animator.walk) &
        load_clip(*locomotion, "run", skeleton, animator.run) &
        load_clip(*locomotion, "jump_start", skeleton, animator.jump_start) &
        load_clip(*locomotion, "jump_loop", skeleton, animator.jump_loop) &
        load_clip(*locomotion, "jump_end", skeleton, animator.jump_end);

    if (!loaded || animator.walk.ground_speed <= 0.0f || animator.run.ground_speed <= animator.walk.ground_speed)
    {
        LogCharacterController.Log("Locomotion set '%s' is incomplete", locomotion_json_path.c_str());
        return false;
    }
    animator.bind_pose = make_bind_pose(skeleton);

    CharacterMovement movement;
    movement.walk_speed = animator.walk.ground_speed;
    movement.run_speed = animator.run.ground_speed;

    const Transform t = scene::get_world_transform(registry, e);
    movement.position = t.position.glm();
    const glm::vec3 facing = t.rotation.glm() * glm::vec3(0, 0, 1);
    movement.heading = std::atan2(facing.x, facing.z);
    movement.move_direction = glm::vec3(std::sin(movement.heading), 0, std::cos(movement.heading));
    movement.previous_position = movement.position;
    movement.previous_heading = movement.heading;

    // capsule as tall as the mesh
    const MeshRenderer* renderer = registry.get<MeshRenderer>(e);
    const AABB bounds = renderer ? get_mesh_bounds(*renderer, t) : AABB::zero();
    float height = bounds.max.y - bounds.min.y;
    if (!(height > 0.5f && height < 3.0f))
        height = 1.7f;

    movement.capsule = world.get_physics().create_character({
        .radius = CharacterMovement::capsule_radius,
        .height = height,
        .position = movement.position + glm::vec3(0.0f, 0.05f, 0.0f),
        // framework convention: user_data is the entity
        .user_data = e.bits(),
    });

    LogCharacterController.Log("Character '%s': walk %.2f m/s, run %.2f m/s, capsule %.2f x %.2f m",
        name.c_str(), movement.walk_speed, movement.run_speed, CharacterMovement::capsule_radius, height);

    registry.add<CharacterMovement>(e, movement);
    registry.add<CharacterInput>(e);
    registry.add<CharacterAnimator>(e, std::move(animator));
    return true;
}

Transform make_character_camera(World& world, ecs::Entity character, float camera_yaw, float camera_pitch)
{
    ecs::Registry& registry = world.registry;
    const glm::vec3 position = scene::get_world_transform(registry, character).position.glm();
    const glm::vec3 pivot = position + glm::vec3(0.0f, camera_pivot_height, 0.0f);
    const glm::vec3 back = -camera_forward(camera_yaw, camera_pitch);

    // sphere sweep from the pivot: the camera stays in front of walls and ceilings
    float distance = camera_distance;
    CharacterCameraProbe* probe = registry.find_resource<CharacterCameraProbe>();
    const CharacterMovement* movement = registry.get<CharacterMovement>(character);
    if (probe && movement)
    {
        phys::PhysicsScene& physics = world.get_physics();
        const phys::BodyId character_body = physics.get_character_body(movement->capsule);
        const phys::QueryFilter filter{
            .categories = phys::Category::static_world | phys::Category::voxel,
            .ignore_bodies = std::span(&character_body, 1),
        };
        if (auto hit = physics.sweep(probe->shape, { pivot }, back, camera_distance, filter))
            distance = std::max(hit->distance, 0.1f);
    }

    Transform t;
    t.position = pivot + back * distance;
    t.rotation = math::from_euler_rotation(glm::vec3(camera_pitch, camera_yaw, 0.0f));
    return t;
}

void install_character_controller(World& world, const Input& input)
{
    scene::register_component_type<PlayerControlled>();
    scene::register_component_type<CharacterInput, false>();
    scene::register_component_type<CharacterMovement, false>();
    scene::register_component_type<CharacterAnimator, false>();

    world.registry.set_resource_ref(input);
    world.registry.on_remove<CharacterMovement>([] (ecs::Registry& registry, ecs::Entity, CharacterMovement& movement) {
        if (phys::PhysicsScene* physics = registry.find_resource<phys::PhysicsScene>())
            physics->destroy_character(movement.capsule);
    });

    world.schedule.add<&read_player_input>(ecs::Phase::FixedPre);
    world.schedule.add<&move_characters>(ecs::Phase::Fixed);
    world.schedule.add<&animate_characters>(ecs::Phase::Update);
    world.schedule.add<&interpolate_characters>(ecs::Phase::Update);
    world.schedule.add<&create_camera_probe>(ecs::Phase::PostLoad);
}
