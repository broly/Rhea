module;

#include <json/value.h>

module character_controller;

import std.compat;
import assertions;
import fixed_string;

import json_utils;
import log;
import name;
import cvar;
import sky_controller;
import animation;
import locomotion;

#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

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

    bool load_clip(const Json::Value& locomotion, const char* role, Clip& clip)
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

        clip.duration = clip.handle.get().duration;
        clip.ground_speed = entry["ground_speed"].asFloat();
        clip.name = role;
        return true;
    }

    // Shared state for the camera probe (one per world is enough)
    struct CharacterCameraProbe
    {
        phys::Shape shape;
    };

    // `player.teleport <x> <y> <z>` (console, Terrain window): the player's feet, applied at the next fixed tick
    std::optional<glm::vec3> pending_teleport;

    // set_scripted_player_input
    std::optional<CharacterInput> scripted_input;

    // Legacy controller (the player is a locomotion character since 2026-10-01): its systems never run on the
    // entities of the locomotion module (ambiguous_with). The console commands player.teleport / player.input
    // live there.

    // ------------------------------------------------------------------ systems

    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::ambiguous_with<loco::LocomotionInputSet>, =ecs::ambiguous_with<loco::LocomotionSimulation>]]
    void teleport_player(ecs::Query<CharacterMovement, const PlayerControlled> characters, ecs::ResMut<phys::PhysicsScene> physics)
    {
        if (!pending_teleport)
            return;
        characters.each([&] (CharacterMovement& movement, const PlayerControlled&) {
            movement.position = movement.previous_position = *pending_teleport;
            movement.vertical_velocity = 0.0f;
            movement.speed = 0.0f;
            physics->set_character_position(movement.capsule, *pending_teleport);
        });
        pending_teleport.reset();
    }

    [[=ecs::system<ecs::Phase::FixedPre>]]
    void read_player_input(ecs::Query<CharacterInput, const PlayerControlled> characters, ecs::Res<Input> input)
    {
        characters.each([&] (CharacterInput& command, const PlayerControlled& player) {
            command = {};
            command.camera_yaw = player.camera_yaw;
            if (!player.enabled)
                return;
            if (scripted_input)
            {
                command.move_axis = scripted_input->move_axis;
                command.walk = scripted_input->walk;
                command.jump = scripted_input->jump;
                return;
            }
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

    // jump / fall / land events of the movement -> jump state machine; moving leaves the pose
    void update_states(CharacterAnimator& a, const CharacterMovement& m, const CharacterInput* input)
    {
        if (input && glm::length(input->move_axis) > 0.0f && a.active_pose >= 0)
            a.select_pose(-1);

        const anim::Transition blend{ .duration = a.jump_blend_time };
        const anim::Transition quick{ .duration = 0.05f };

        if (m.jump_count != a.seen_jump_count)
        {
            a.seen_jump_count = m.jump_count;
            a.jump.force_transition(JumpState::start, blend);
        }
        if (m.land_count != a.seen_land_count)
        {
            a.seen_land_count = m.land_count;
            if (!a.jump.in(JumpState::none))
                a.jump.force_transition(JumpState::land, quick);
        }
        // walked off a ledge
        if (a.jump.in(JumpState::none) && !m.grounded && m.air_time > a.fall_animation_delay)
            a.jump.transition(JumpState::loop, blend);

        if (a.jump.in(JumpState::start) && a.jump_start_player.clip && a.jump_start_player.remaining() <= 0.0f)
            a.jump.transition(JumpState::loop, quick);
        if (a.jump.in(JumpState::land) && a.jump_end_player.clip && a.jump_end_player.remaining() <= a.jump_blend_time)
            a.jump.transition(JumpState::none, blend);
    }

    // idle -> walk -> run by speed (walk and run phase synchronized), poses over it, jumps over everything
    anim::PoseRef character_pose(anim::Graph& g, CharacterAnimator& a, const CharacterMovement& m)
    {
        auto locomotion = [&] {
            if (m.speed <= a.walk.ground_speed)
            {
                return g.blend(m.speed / a.walk.ground_speed,
                    [&] { return g.play(a.idle_player, a.idle.handle); },
                    [&] { return g.play(a.walk_player, a.walk.handle, { .sync = "cycle" }); });
            }
            // the cycle length goes from walk to run with the weight: both players run at that rate
            const float w = std::clamp((m.speed - a.walk.ground_speed) / (a.run.ground_speed - a.walk.ground_speed), 0.0f, 1.0f);
            const float cycle = glm::mix(a.walk.duration, a.run.duration, w);
            return g.blend(w,
                [&] { return g.play(a.walk_player, a.walk.handle, { .rate = a.walk.duration / cycle, .sync = "cycle" }); },
                [&] { return g.play(a.run_player, a.run.handle, { .rate = a.run.duration / cycle, .sync = "cycle" }); });
        };

        auto with_poses = [&] {
            if (a.poses.empty())
                return locomotion();
            return g.blend_list(a.pose_list, a.active_pose + 1, (uint32_t)a.poses.size() + 1, a.pose_blend_time,
                [&](uint32_t i) { return i == 0 ? locomotion() : g.play(a.pose_players[i - 1], a.poses[i - 1].handle); });
        };

        return g.state_machine(a.jump, [&](JumpState s) {
            switch (s)
            {
            case JumpState::start: return g.play(a.jump_start_player, a.jump_start.handle, { .loop = false });
            case JumpState::loop: return g.play(a.jump_loop_player, a.jump_loop.handle);
            case JumpState::land: return g.play(a.jump_end_player, a.jump_end.handle, { .loop = false });
            case JumpState::none: break;
            }
            return with_poses();
        });
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

    [[=ecs::system<ecs::Phase::Fixed>, =ecs::ambiguous_with<loco::LocomotionSimulation>]]
    void move_characters(ecs::Query<CharacterMovement, const CharacterInput> characters,
        ecs::ResMut<phys::PhysicsScene> physics, ecs::Res<ecs::SimTime> time)
    {
        characters.each([&] (CharacterMovement& movement, const CharacterInput& command) {
            move_character(movement, command, (float)time->dt, *physics);
        });
    }

    // the probe shape is made once the physics scene exists (after the level is loaded)
    // independent of the colliders, but both take the Registry
    [[=ecs::system<ecs::Phase::PostLoad>, =ecs::ambiguous_with<MeshColliderSync>]]
    void create_camera_probe(ecs::Registry& registry, ecs::ResMut<phys::PhysicsScene> physics)
    {
        if (!registry.find_resource<CharacterCameraProbe>())
            registry.set_resource<CharacterCameraProbe>(physics->create_shape(phys::SphereShape{ camera_probe_radius }));
    }

    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<anim::Evaluate>, =ecs::ambiguous_with<loco::LocomotionPresentation>]]
    void animate_characters(ecs::Query<anim::Animator, CharacterAnimator, SkinnedMesh, const CharacterMovement> characters,
        ecs::Query<const CharacterInput> inputs, ecs::Res<ecs::FrameTime> time)
    {
        const float dt = std::min((float)time->dt, 0.1f);   // hitches (shader compilation, loading)
        characters.each(
            [&] (ecs::Entity e, anim::Animator& animator, CharacterAnimator& a, SkinnedMesh& mesh, const CharacterMovement& movement)
            {
                if (!animator.valid())
                    return;
                update_states(a, movement, inputs.get(e));
                anim::Graph g = animator.begin_update(dt);
                animator.end_update(g, character_pose(g, a, movement));
                update_expression(a, mesh, dt);
            });
    }

    // Render transform between the last two simulated ticks (spring bones simulate in world space from it)
    // (the sky controller writes Transform too: of the sun, never of a character)
    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<CharacterInterpolation>, =ecs::before<anim::PostProcess>, =ecs::ambiguous_with<SkyControl>,
      =ecs::ambiguous_with<loco::LocomotionPresentation>]]
    void interpolate_characters(ecs::Query<Transform, const CharacterMovement> characters, ecs::Res<ecs::FrameTime> time)
    {
        const float alpha = (float)time->alpha;
        characters.each([&] (Transform& transform, const CharacterMovement& m) {
            transform.position = glm::mix(m.previous_position, m.position, alpha);
            const float heading = m.previous_heading + wrap_angle(m.heading - m.previous_heading) * alpha;
            transform.rotation = glm::angleAxis(heading, glm::vec3(0, 1, 0));
        });
    }

    [[=ecs::on_remove]]
    void destroy_character_capsule(ecs::Registry& registry, ecs::Entity, CharacterMovement& movement)
    {
        if (phys::PhysicsScene* physics = registry.find_resource<phys::PhysicsScene>())
            physics->destroy_character(movement.capsule);
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(PlayerControlled, CharacterInput, CharacterMovement, CharacterAnimator)
}


void CharacterAnimator::load_poses(const std::string& poses_json_path)
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
        clip.duration = clip.handle.get().duration;
        clip.name = entry["name"].asString();
        poses.push_back(std::move(clip));
    }
    pose_players.resize(poses.size());
    pose_list.reset_on_activation = true;   // a pose starts from its beginning
    LogCharacterController.Log("Loaded %zu poses", poses.size());
}

void CharacterAnimator::select_pose(int32_t pose_index)
{
    if (pose_index >= (int32_t)poses.size() || pose_index == active_pose)
        return;

    // the graph crossfades from the current pose (or locomotion)
    active_pose = pose_index;

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
        load_clip(*locomotion, "idle", animator.idle) &
        load_clip(*locomotion, "walk", animator.walk) &
        load_clip(*locomotion, "run", animator.run) &
        load_clip(*locomotion, "jump_start", animator.jump_start) &
        load_clip(*locomotion, "jump_loop", animator.jump_loop) &
        load_clip(*locomotion, "jump_end", animator.jump_end);

    if (!loaded || animator.walk.ground_speed <= 0.0f || animator.run.ground_speed <= animator.walk.ground_speed)
    {
        LogCharacterController.Log("Locomotion set '%s' is incomplete", locomotion_json_path.c_str());
        return false;
    }
    anim::Animator graph_animator;
    graph_animator.init(anim::Rig::create(skeleton));

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
    registry.add<anim::Animator>(e, std::move(graph_animator));
    return true;
}

void set_scripted_player_input(const std::optional<CharacterInput>& input)
{
    scripted_input = input;
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
