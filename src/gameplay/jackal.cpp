module;

#include <json/value.h>

module gameplay;

import :jackal;
import :health;
import :player;
import :weapons;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import rhcomponents;
import physics;
import animation;
import navigation;
import assets;
import cvar;
import json_utils;
import fixed_string;
import log;

#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogJackal, Log);

bool JackalClips::load(const std::string& json_path)
{
    std::optional<Json::Value> json = json_utils::load_json_asset(json_path);
    if (!json)
    {
        LogJackal.Log("No jackal locomotion file '%s' (tools/creatures/import_jackal.py)", json_path.c_str());
        return false;
    }
    AssetManager& assets = AssetManager::get();
    const Json::Value& gait_list = (*json)["gaits"];
    for (const std::string& name : gait_list.getMemberNames())
    {
        const Json::Value& entry = gait_list[name];
        AnimationClipHandle clip = assets.load_animation(entry["clip"].asString());
        if (!clip.is_valid())
            continue;
        if (name == "idle")
            idle = clip;
        else
            gaits.push_back({ clip, entry["speed"].asFloat(), entry["stride"].asFloat() });
    }
    std::ranges::sort(gaits, {}, &Gait::speed);
    const Json::Value& other = (*json)["clips"];
    if (other.isMember("death"))
        death = assets.load_animation(other["death"].asString());
    if (!idle.is_valid() || gaits.empty() || !death.is_valid())
    {
        LogJackal.Log("Jackal clips incomplete in '%s': idle %d, gaits %zu, death %d", json_path.c_str(), idle.is_valid(),
            gaits.size(), death.is_valid());
        return false;
    }
    gaits.resize(std::min<size_t>(gaits.size(), std::tuple_size_v<decltype(JackalAnimation::gait_players)>));
    return true;
}

namespace
{
    cvar::Var<bool> cv_chase("game.jackal.chase", true, "Jackals run at the player (off: they stand still)");
    cvar::Var<std::string> cv_clips("game.jackal.clips", "animations/jackal/locomotion.json", "Gaits and clips of the jackals");

    constexpr float gravity = 9.81f;
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr glm::vec3 up{ 0.0f, 1.0f, 0.0f };

    float wrap_angle(float a)
    {
        return std::remainder(a, 2.0f * pi);
    }

    float lerp_angle(float from, float to, float t)
    {
        return from + wrap_angle(to - from) * t;
    }

    float move_towards(float value, float target, float max_delta)
    {
        return value + std::clamp(target - value, -max_delta, max_delta);
    }

    // exponential smoothing that does not depend on the tick rate
    float smooth(float value, float target, float rate, float dt)
    {
        return glm::mix(value, target, 1.0f - std::exp(-rate * dt));
    }

    glm::vec3 forward_of(float heading)
    {
        return { std::sin(heading), 0.0f, std::cos(heading) };
    }

    // yaw (+z turns to +x: left), nose up by `pitch`, rolled by `lean` (> 0: the back to the right)
    glm::quat body_rotation(float heading, float pitch, float lean)
    {
        return glm::angleAxis(heading, up) * glm::angleAxis(-pitch, glm::vec3(1, 0, 0)) * glm::angleAxis(lean, glm::vec3(0, 0, 1));
    }

    phys::QueryFilter ground_filter()
    {
        phys::QueryFilter filter;
        filter.categories = phys::Category::static_world | phys::Category::voxel;
        return filter;
    }

    // the floor under `p`, searched from a step above it
    std::optional<phys::Hit> find_ground(const phys::PhysicsScene& physics, const glm::vec3& p, float step)
    {
        std::optional<phys::Hit> hit = physics.raycast(p + up * step, -up, step + 1.5f, ground_filter());
        if (hit && hit->normal.y < 0.5f)
            return std::nullopt;   // a wall, not a floor
        return hit;
    }

    std::shared_ptr<const JackalClips> shared_clips(const std::string& path)
    {
        static std::map<std::string, std::shared_ptr<const JackalClips>> cache;
        if (auto it = cache.find(path); it != cache.end())
            return it->second;
        std::shared_ptr<const JackalClips> result;
        if (auto clips = std::make_shared<JackalClips>(); clips->load(path))
            result = std::move(clips);
        cache[path] = result;
        return result;
    }

    // ---------------------------------------------------------------- spawn

    [[=scene::on_spawned<Jackal>]]
    void init_jackal(World& world, ecs::Entity e, const SerializationContext&)
    {
        ecs::Registry& registry = world.registry;
        SkinnedMesh* skinned = registry.get<SkinnedMesh>(e);
        if (!skinned || !skinned->skeletal_mesh.is_valid())
        {
            LogJackal.Log("Jackal '%s' has no skinned mesh: not animated", scene::get_name(registry, e).c_str());
            return;
        }
        if (!skinned->pose)   // the SkinnedMesh may be spawned after us
        {
            init_skinned_mesh(registry, e);
            skinned = registry.get<SkinnedMesh>(e);
        }

        std::shared_ptr<const JackalClips> clips = shared_clips(cv_clips.get());
        if (!clips)
            return;

        std::shared_ptr<anim::Rig> rig = anim::Rig::create(skinned->get_skeleton());
        anim::Animator animator;
        animator.init(rig);

        JackalAnimation a;
        a.clips = clips;
        a.moving = { .interpolate = true, .speed_increasing = 8.0f, .speed_decreasing = 5.0f };
        const char* spine[] = { "Bip001-Spine", "Bip001-Spine1", "Bip001-Spine2", "Bip001-Neck", "Bip001-Neck1", "Bip001-Head" };
        for (size_t i = 0; i < a.spine.size(); ++i)
            a.spine[i] = rig->find_bone(spine[i]).transform([] (uint32_t b) { return int32_t(b); }).value_or(-1);
        const char* tail[] = { "B_Bip001-Tail00", "B_Bip001-Tail01", "B_Bip001-Tail02", "B_Bip001-Tail03", "B_Bip001-Tail04" };
        for (size_t i = 0; i < a.tail.size(); ++i)
            a.tail[i] = rig->find_bone(tail[i]).transform([] (uint32_t b) { return int32_t(b); }).value_or(-1);
        // model space = below the skeleton's root transform (asset axes and scale)
        a.up_model = glm::normalize(glm::inverse(glm::mat3(rig->skeleton->root_transform)) * up);

        registry.add<anim::Animator>(e, std::move(animator));
        registry.add<JackalAnimation>(e, std::move(a));
    }

    [[=ecs::on_remove]]
    void destroy_jackal_hitbox(ecs::Registry& registry, ecs::Entity, Jackal& jackal)
    {
        phys::PhysicsScene* physics = registry.find_resource<phys::PhysicsScene>();
        if (jackal.hitbox.is_valid() && physics)
            physics->destroy_body(jackal.hitbox);
    }

    // ---------------------------------------------------------------- simulation

    // Steering of one tick: where the jackal wants to go and how fast (0: stand), world direction
    struct Steering
    {
        glm::vec3 direction{ 0.0f };
        float speed = 0.0f;
        bool engaged = false;   // the player is in reach: faces it even when standing
    };

    bool wants_to_chase(const Jackal& j, float distance)
    {
        return j.chase && cv_chase.get() && distance <= j.aggro_radius;
    }

    Steering steer(const Jackal& j, const std::optional<glm::vec3>& player, std::span<const glm::vec3> others, const nav::NavAgent* agent)
    {
        Steering s;
        if (!player)
            return s;
        glm::vec3 to_player = *player - j.position;
        to_player.y = 0.0f;
        const float distance = glm::length(to_player);
        if (distance < 1e-3f)
            return s;
        if (!wants_to_chase(j, distance))
            return s;   // keeps its heading, stands
        s.engaged = true;
        s.direction = to_player / distance;
        float remaining = distance - j.stop_distance;

        // The way there: along the navigation mesh, around walls, and around the pack and the others (the crowd's
        // avoidance). Straight at the player in front of it, or without a path (no mesh, off it).
        const bool on_path = agent && (nav::is_following_path(*agent) || agent->status == nav::NavStatus::planning)
            && distance > j.stop_distance + 0.5f;
        if (on_path)
        {
            glm::vec3 way(agent->desired_velocity.x, 0.0f, agent->desired_velocity.z);
            if (glm::length(way) < 0.1f && agent->has_corner)
                way = glm::vec3(agent->next_corner.x - j.position.x, 0.0f, agent->next_corner.z - j.position.z);
            if (glm::length(way) > 1e-3f)
                s.direction = glm::normalize(way);
            // brakes for the end of the path (the closest it gets when the player stands out of reach)
            if (std::isfinite(agent->distance_to_goal))
                remaining = agent->distance_to_goal - j.stop_distance;
        }

        // brakes in time to stop at stop_distance: v = sqrt(2 a d)
        if (remaining > 0.0f)
            s.speed = std::min(j.run_speed, std::sqrt(2.0f * j.deceleration * remaining));
        if (on_path)
            return s;   // the crowd keeps the pack apart

        // keeps apart from the pack: pushed away by the ones closer than two bodies
        glm::vec3 push{ 0.0f };
        for (const glm::vec3& other : others)
        {
            glm::vec3 away = j.position - other;
            away.y = 0.0f;
            const float d = glm::length(away);
            const float reach = 4.0f * j.radius;
            if (d > 1e-3f && d < reach)
                push += away / d * (1.0f - d / reach);
        }
        const glm::vec3 steered = s.direction + push * 1.5f;
        if (glm::length(steered) > 1e-3f)
            s.direction = glm::normalize(steered);
        return s;
    }

    void simulate(Jackal& j, const Steering& steering, bool dead, phys::PhysicsScene& physics, float dt)
    {
        j.previous_position = j.position;
        j.previous_heading = j.heading;
        j.previous_pitch = j.pitch;
        j.previous_lean = j.lean_angle;

        float desired_speed = dead ? 0.0f : steering.speed;
        float target_rate = 0.0f;
        if (!dead && steering.engaged)
        {
            const float desired_heading = std::atan2(steering.direction.x, steering.direction.z);
            const float diff = wrap_angle(desired_heading - j.heading);
            // animals slow down for sharp turns instead of turning on a point at full speed
            const float sharp = std::clamp((std::abs(diff) - glm::radians(45.0f)) / glm::radians(90.0f), 0.0f, 1.0f);
            desired_speed *= glm::mix(1.0f, 0.3f, sharp);
            // standing in front of the player: turns to face it only past a few degrees
            if (steering.speed > 0.0f || std::abs(diff) > glm::radians(20.0f) || std::abs(j.yaw_rate) > 0.1f)
            {
                const float speed_alpha = std::clamp(j.speed / std::max(j.run_speed, 0.1f), 0.0f, 1.0f);
                const float max_rate = glm::radians(glm::mix(j.turn_rate, j.turn_rate_at_speed, speed_alpha));
                target_rate = std::clamp(diff * 5.0f, -max_rate, max_rate);
            }
        }
        j.desired_speed = desired_speed;
        j.yaw_rate = move_towards(j.yaw_rate, target_rate, glm::radians(j.turn_acceleration) * dt);
        j.heading = wrap_angle(j.heading + j.yaw_rate * dt);
        j.speed = move_towards(j.speed, desired_speed, (desired_speed > j.speed ? j.acceleration : j.deceleration) * dt);

        // move, sliding along walls (a ray at chest height ahead of the body)
        const glm::vec3 forward = forward_of(j.heading);
        glm::vec3 step = forward * (j.speed * dt);
        if (j.speed > 0.01f)
        {
            phys::QueryFilter walls = ground_filter();
            const glm::vec3 chest = j.position + up * 0.3f;
            if (std::optional<phys::Hit> wall = physics.raycast(chest, forward, j.radius + glm::length(step), walls))
            {
                glm::vec3 n = wall->normal;
                n.y = 0.0f;
                if (glm::length(n) > 1e-3f && wall->normal.y < 0.5f)
                {
                    n = glm::normalize(n);
                    step -= n * std::min(glm::dot(step, n), 0.0f);
                    // head on: it stops, along the wall it keeps running
                    j.speed *= std::clamp(1.0f + glm::dot(forward, n), 0.2f, 1.0f);
                }
            }
        }

        glm::vec3 next = j.position + step;
        constexpr float max_step = 0.45f;   // m, stairs yes, walls no
        if (std::optional<phys::Hit> ground = find_ground(physics, next, max_step))
        {
            if (ground->position.y - j.position.y <= max_step)
                next.y = ground->position.y;
            else
                next = j.position;
        }
        j.position = next;

        // body along the slope: floor under the front and the hind paws
        const float half_length = 0.3f;
        std::optional<phys::Hit> front = find_ground(physics, j.position + forward * half_length, max_step);
        std::optional<phys::Hit> back = find_ground(physics, j.position - forward * half_length, max_step);
        float target_pitch = 0.0f;
        if (front && back)
            target_pitch = std::atan2(front->position.y - back->position.y, 2.0f * half_length);
        j.pitch = smooth(j.pitch, std::clamp(target_pitch, -0.6f, 0.6f), 12.0f, dt);

        // leans into turns like a running animal: tan(roll) = centripetal acceleration / g
        const float target_lean = -std::atan(j.speed * j.yaw_rate / gravity) * j.lean;
        j.lean_angle = smooth(j.lean_angle, std::clamp(target_lean, -0.35f, 0.35f), 10.0f, dt);
    }

    void update_hitbox(ecs::Entity e, Jackal& j, phys::PhysicsScene& physics)
    {
        const phys::BodyTransform pose{ j.position, body_rotation(j.heading, j.pitch, 0.0f) };
        if (j.hitbox.is_valid())
        {
            physics.move_kinematic(j.hitbox, pose);
            return;
        }
        if (!j.hitbox_shape)
            j.hitbox_shape = physics.create_shape(phys::BoxShape{ .half_extents = j.hitbox_half_extents.glm(), .center = j.hitbox_center.glm() });
        j.hitbox = physics.create_body({
            .shape = j.hitbox_shape,
            .position = pose.position,
            .rotation = pose.rotation,
            .motion = phys::Motion::kinematic,
            .category = phys::Category::hitbox,
            .user_data = e.bits(),
        });
    }

    // before the weapons aim at the hitboxes, the damage and the physics step
    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::in_set<JackalSimulation>, =ecs::before<WeaponFire>, =ecs::before<DamageResolution>,
      =ecs::before<scene::PhysicsStep>]]
    void simulate_jackals(ecs::Query<Jackal, const Transform> jackals, ecs::Query<const WorldTransform, ecs::With<Player>> players,
        ecs::Query<const Dead> dead, ecs::Query<nav::NavAgent> agents, ecs::ResMut<phys::PhysicsScene> physics, ecs::Res<ecs::SimTime> time)
    {
        std::optional<glm::vec3> player;
        players.each([&] (const WorldTransform& transform) {
            if (!player)
                player = transform.value.position.glm();
        });

        std::vector<glm::vec3> positions;
        jackals.each([&] (Jackal& j, const Transform& transform) {
            if (!j.initialized)
            {
                j.initialized = true;
                j.position = j.previous_position = transform.position.glm();
                const glm::vec3 f = transform.rotation.glm() * glm::vec3(0, 0, 1);
                j.heading = j.previous_heading = std::atan2(f.x, f.z);
                if (std::optional<phys::Hit> ground = find_ground(*physics, j.position, 0.5f))
                    j.position.y = j.previous_position.y = ground->position.y;
            }
            positions.push_back(j.position);
        });

        const float dt = (float)time->dt;
        size_t index = 0;
        jackals.each([&] (ecs::Entity e, Jackal& j, const Transform&) {
            // the others: all but itself
            std::vector<glm::vec3> others;
            others.reserve(positions.size());
            for (size_t i = 0; i < positions.size(); ++i)
                if (i != index && glm::length(positions[i] - j.position) < 2.0f)
                    others.push_back(positions[i]);
            ++index;

            if (player)
                j.target_distance = glm::length(glm::vec2(player->x - j.position.x, player->z - j.position.z));
            const bool is_dead = dead.get(e) != nullptr;

            // the crowd plans the way to the player (next tick's NavAgentUpdate); the dead lie in the way of the others
            nav::NavAgent* agent = agents.get(e);
            if (agent)
            {
                if (!is_dead && player && wants_to_chase(j, j.target_distance))
                    nav::move_to(*agent, *player);
                else
                    nav::stop(*agent);
                agent->obstacle = is_dead;
                agent->max_speed = j.run_speed;
            }

            simulate(j, steer(j, player, others, agent), is_dead, *physics, dt);
            update_hitbox(e, j, *physics);
            if (agent)
            {
                agent->position = j.position;
                agent->velocity = forward_of(j.heading) * j.speed;
            }
        });
    }

    // between the last two ticks, before the transforms are propagated (and before bone attachments read them)
    [[=ecs::system<ecs::Phase::Late>, =ecs::before<scene::TransformPropagation>, =ecs::before<BoneAttachments>]]
    void interpolate_jackals(ecs::Query<Transform, const Jackal> jackals, ecs::Res<ecs::FrameTime> time)
    {
        const float alpha = (float)time->alpha;
        jackals.each([&] (Transform& transform, const Jackal& j) {
            if (!j.initialized)
                return;
            transform.position = glm::mix(j.previous_position, j.position, alpha);
            transform.rotation = body_rotation(lerp_angle(j.previous_heading, j.heading, alpha), glm::mix(j.previous_pitch, j.pitch, alpha),
                glm::mix(j.previous_lean, j.lean_angle, alpha));
        });
    }

    // ---------------------------------------------------------------- animation

    anim::PoseRef gait_pose(anim::Graph& g, JackalAnimation& a, float speed)
    {
        const std::vector<JackalClips::Gait>& gaits = a.clips->gaits;
        const uint32_t count = (uint32_t)gaits.size();

        // place among the gaits: i and the fraction towards i + 1
        uint32_t i = 0;
        float fraction = 0.0f;
        if (speed >= gaits.back().speed)
            i = count - 1;
        else if (speed > gaits.front().speed)
        {
            while (i + 1 < count && speed >= gaits[i + 1].speed)
                ++i;
            fraction = (speed - gaits[i].speed) / std::max(gaits[i + 1].speed - gaits[i].speed, 1e-3f);
        }
        // a gait is its own motion, a half walk half trot looks soft: they cross over in the middle half of the interval
        fraction = glm::smoothstep(0.25f, 0.75f, fraction);
        a.gait_position = float(i) + fraction;

        std::array<float, std::tuple_size_v<decltype(JackalAnimation::gait_players)>> weights{};
        weights[i] = 1.0f - fraction;
        if (i + 1 < count)
            weights[i + 1] = fraction;

        // The leader of the sync group (highest weight) sets the cycle frequency, the others follow its phase: the
        // blend covers sum(weight * stride) per cycle. Rate so that this matches the speed - the paws on the ground stay put.
        const uint32_t leader = (i + 1 < count && fraction > 0.5f) ? i + 1 : i;
        float stride = 0.0f;
        for (uint32_t k = 0; k < count; ++k)
            stride += weights[k] * gaits[k].stride;
        const float leader_duration = gaits[leader].clip.get().duration;
        a.rate = stride > 1e-3f ? std::clamp(speed * leader_duration / stride, 0.35f, 2.2f) : 1.0f;

        return g.blend_n(std::span<const float>(weights.data(), count), [&] (uint32_t k) {
            return g.play(a.gait_players[k], gaits[k].clip, { .rate = a.rate, .sync = "JackalGait" });
        });
    }

    // Bends the spine (to the head) into the turn and swings the tail out, about the body's up axis
    void bend_body(const anim::Rig& rig, JackalAnimation& a, anim::Pose& pose, BonePose& model)
    {
        if (std::abs(a.bend) < 1e-4f)
            return;
        rig.to_model(pose.bones, model);
        auto apply = [&] (std::span<const int32_t> chain, std::span<const float> shares, float angle) {
            for (size_t c = 0; c < chain.size(); ++c)
            {
                const int32_t b = chain[c];
                if (b < 0)
                    continue;
                const int32_t p = rig.parents[b];
                const glm::quat parent_rotation = p >= 0 ? model[p].rotation : glm::quat(1, 0, 0, 0);
                const glm::quat rotation = glm::angleAxis(angle * shares[c], a.up_model) * (parent_rotation * pose.bones[b].rotation);
                pose.bones[b].rotation = glm::normalize(glm::inverse(parent_rotation) * rotation);
                model[b].rotation = rotation;   // the next bone of the chain is its child
            }
        };
        static constexpr float spine_shares[] = { 0.1f, 0.15f, 0.2f, 0.2f, 0.2f, 0.15f };
        static constexpr float tail_shares[] = { 0.3f, 0.25f, 0.2f, 0.15f, 0.1f };
        apply(a.spine, spine_shares, a.bend);
        apply(a.tail, tail_shares, -0.8f * a.bend);
    }

    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<anim::Evaluate>, =ecs::ambiguous_with<anim::Evaluate>]]
    void animate_jackals(ecs::Query<anim::Animator, JackalAnimation, const Jackal> jackals, ecs::Query<const Dead> dead,
        ecs::Res<ecs::FrameTime> time)
    {
        const float dt = std::min((float)time->dt, 0.1f);   // hitches (shader compilation, loading)
        thread_local BonePose model;
        jackals.each([&] (ecs::Entity e, anim::Animator& animator, JackalAnimation& a, const Jackal& j) {
            if (!animator.valid() || !a.clips)
                return;
            const JackalClips& clips = *a.clips;
            a.states.transition(dead.get(e) ? JackalAnimation::State::dead : JackalAnimation::State::alive, { .duration = 0.2f });

            // paws step when it turns on the spot too: the body turns around a point ~0.25 m from the paws
            const float step_speed = std::max(j.speed, std::abs(j.yaw_rate) * 0.25f);
            a.animation_speed = smooth(a.animation_speed, step_speed, 12.0f, dt);
            const float moving = a.moving.update(step_speed > 0.08f ? 1.0f : 0.0f, dt);

            // spine into the turn: more the faster it turns, springy
            const float target_bend = std::clamp(j.yaw_rate * 0.3f, -0.6f, 0.6f);
            anim::spring_damper(a.bend, a.bend_velocity, target_bend, 0.0f, dt, 2.5f, 0.9f);

            anim::Graph g = animator.begin_update(dt);
            anim::PoseRef pose = g.state_machine(a.states, [&] (JackalAnimation::State state) {
                if (state == JackalAnimation::State::dead)
                    return g.play(a.death, clips.death, { .loop = false });
                return g.blend(moving,
                    [&] { return g.play(a.idle, clips.idle); },
                    [&] { return gait_pose(g, a, std::max(a.animation_speed, 0.05f)); });
            });
            if (!dead.get(e))
                bend_body(*animator.rig, a, *pose, model);
            animator.end_update(g, std::move(pose));

            char line[96];
            std::snprintf(line, sizeof(line), "%.2f m/s (wants %.2f), turn %.0f deg/s", j.speed, j.desired_speed, glm::degrees(j.yaw_rate));
            animator.debug_value("movement", line);
            std::snprintf(line, sizeof(line), "gait %.2f, rate %.2f, moving %.2f, bend %.1f deg", a.gait_position, a.rate, moving,
                glm::degrees(a.bend));
            animator.debug_value("animation", line);
            std::snprintf(line, sizeof(line), "%.1f m to the player, pitch %.1f, lean %.1f deg", j.target_distance, glm::degrees(j.pitch),
                glm::degrees(j.lean_angle));
            animator.debug_value("target", line);
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Jackal, JackalAnimation)
}
