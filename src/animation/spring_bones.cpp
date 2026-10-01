module;

#include <json/value.h>

module animation;

import std.compat;
import glm;
import assets;
import ecs;
import framework;
import rhmath;
import json_utils;
import log;
import debug_draw;
import profile;
import fixed_string;

#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogSpringBones, Log);

namespace anim
{
    namespace
    {
        constexpr float substep_rate = 120.0f;    // substeps per second (fast limbs must not skip over hair)
        constexpr float teleport_distance = 1.0f; // root moved further in a frame: restart from the animated pose

        glm::vec3 read_vec3(const Json::Value& v, const glm::vec3& fallback = glm::vec3(0.0f))
        {
            if (!v.isArray() || v.size() != 3)
                return fallback;
            return glm::vec3(v[0].asFloat(), v[1].asFloat(), v[2].asFloat());
        }

        // model space offset at the bind pose -> bone space
        glm::vec3 to_bone_space(const BoneTransform& bind_model, const glm::vec3& offset)
        {
            return (glm::inverse(bind_model.rotation) * offset) / bind_model.scale;
        }

        glm::vec3 bone_point(const BoneTransform& model, const glm::vec3& offset)
        {
            return model.translation + model.rotation * (model.scale * offset);
        }

        // Closest points of segments p1-q1 and p2-q2 (Ericson, Real-Time Collision Detection 5.1.9):
        // s, t parameters on each
        void closest_points(const glm::vec3& p1, const glm::vec3& q1, const glm::vec3& p2, const glm::vec3& q2, float& s, float& t)
        {
            const glm::vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
            const float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
            constexpr float eps = 1e-10f;
            if (a <= eps && e <= eps)
            {
                s = t = 0.0f;
                return;
            }
            if (a <= eps)
            {
                s = 0.0f;
                t = std::clamp(f / e, 0.0f, 1.0f);
                return;
            }
            const float c = glm::dot(d1, r);
            if (e <= eps)
            {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
                return;
            }
            const float b = glm::dot(d1, d2);
            const float denom = a * e - b * b;
            s = denom > eps ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f)
            {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }

        float distance_to_segment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b)
        {
            const glm::vec3 ab = b - a;
            const float len2 = glm::dot(ab, ab);
            const float t = len2 > 1e-10f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
            return glm::length(p - (a + ab * t));
        }

        // Pushes `tip` (segment root -> tip, root fixed) out of a collider (segment a-b with radius).
        // Returns true when it moved; `normal` points out of the collider.
        bool push_out(const glm::vec3& root, glm::vec3& tip, const glm::vec3& a, const glm::vec3& b, float min_distance, glm::vec3& normal)
        {
            float s, t;
            closest_points(root, tip, a, b, s, t);
            const glm::vec3 on_segment = root + (tip - root) * s;
            const glm::vec3 on_collider = a + (b - a) * t;
            glm::vec3 n = on_segment - on_collider;
            const float d = glm::length(n);
            if (d >= min_distance)
                return false;
            if (d > 1e-6f)
                n /= d;
            else
            {
                // on the axis: away from the collider's middle, sideways to it
                n = tip - (a + b) * 0.5f;
                const glm::vec3 axis = b - a;
                if (glm::dot(axis, axis) > 1e-10f)
                    n -= axis * (glm::dot(n, axis) / glm::dot(axis, axis));
                if (glm::dot(n, n) < 1e-10f)
                    n = glm::vec3(0, 0, -1);
                n = glm::normalize(n);
            }
            // the segment turns around its root: the tip moves more than the contact point
            tip += n * ((min_distance - d) / std::max(s, 0.25f));
            normal = n;
            return true;
        }

        void draw_capsule(const glm::vec3& a, const glm::vec3& b, float r, const glm::vec4& color)
        {
            debug_draw::sphere(a, r, color);
            if (glm::length(b - a) < 1e-4f)
                return;
            debug_draw::sphere(b, r, color);
            const glm::vec3 axis = glm::normalize(b - a);
            glm::vec3 side = glm::cross(axis, glm::vec3(0, 1, 0));
            if (glm::dot(side, side) < 1e-4f)
                side = glm::cross(axis, glm::vec3(1, 0, 0));
            side = glm::normalize(side);
            const glm::vec3 up = glm::cross(axis, side);
            for (const glm::vec3& o : { side, -side, up, -up })
                debug_draw::line(a + o * r, b + o * r, color);
        }
    }

    bool SpringBones::load(const Rig& rig)
    {
        loaded = true;   // a broken config is not retried every frame (`reload` retries)
        needs_reset = true;
        chains.clear();
        colliders.clear();
        jiggles.clear();

        std::optional<Json::Value> json = json_utils::load_json_asset(config);
        if (!json)
        {
            LogSpringBones.Log("Spring bones: can't read '%s'", config.c_str());
            return false;
        }

        BonePose bind_model;
        rig.to_model(rig.bind_pose, bind_model);

        for (const Json::Value& c : (*json)["colliders"])
        {
            SpringCollider collider;
            collider.name = c["name"].asString();
            const std::optional<uint32_t> bone = rig.find_bone(c["bone"].asString());
            if (!bone)
            {
                LogSpringBones.Log("Spring collider '%s': unknown bone '%s'", collider.name.c_str(), c["bone"].asString().c_str());
                continue;
            }
            collider.bone = *bone;
            collider.radius = c.get("radius", 0.05f).asFloat();
            collider.offset = to_bone_space(bind_model[*bone], read_vec3(c["offset"]));
            collider.capsule = c.isMember("to") || c.isMember("to_offset");
            collider.to_bone = *bone;
            if (c.isMember("to"))
            {
                const std::optional<uint32_t> to = rig.find_bone(c["to"].asString());
                if (!to)
                {
                    LogSpringBones.Log("Spring collider '%s': unknown bone '%s'", collider.name.c_str(), c["to"].asString().c_str());
                    continue;
                }
                collider.to_bone = *to;
            }
            collider.to_offset = to_bone_space(bind_model[collider.to_bone], read_vec3(c["to_offset"]));
            colliders.push_back(std::move(collider));
        }

        std::vector<uint32_t> child_count(rig.num_bones(), 0), first_child(rig.num_bones(), 0);
        for (uint32_t bone = 0; bone < rig.num_bones(); ++bone)
            if (const int32_t parent = rig.parents[bone]; parent >= 0)
                if (child_count[parent]++ == 0)
                    first_child[parent] = bone;

        for (const Json::Value& c : (*json)["chains"])
        {
            SpringChain proto;
            proto.name = c["name"].asString();
            proto.enabled = c.get("enabled", true).asBool();
            proto.stiffness = c.get("stiffness", proto.stiffness).asFloat();
            proto.damping = c.get("damping", proto.damping).asFloat();
            proto.drag = c.get("drag", proto.drag).asFloat();
            proto.gravity = c.get("gravity", proto.gravity).asFloat();
            proto.radius = c.get("radius", proto.radius).asFloat();
            proto.follow = std::clamp(c.get("follow", proto.follow).asFloat(), 0.0f, 1.0f);
            proto.max_angle = c.get("max_angle", proto.max_angle).asFloat();
            const float tip_length = c.get("tip_length", 1.0f).asFloat();

            if (c["colliders"].isArray())
            {
                for (const Json::Value& name : c["colliders"])
                {
                    auto it = std::ranges::find(colliders, name.asString(), &SpringCollider::name);
                    if (it != colliders.end())
                        proto.colliders.push_back((uint32_t)(it - colliders.begin()));
                    else
                        LogSpringBones.Log("Spring chain '%s': unknown collider '%s'", proto.name.c_str(), name.asString().c_str());
                }
            }
            else
            {
                for (uint32_t i = 0; i < colliders.size(); ++i)
                    proto.colliders.push_back(i);
            }

            for (const Json::Value& root_name : c["roots"])
            {
                const std::optional<uint32_t> root = rig.find_bone(root_name.asString());
                if (!root)
                {
                    LogSpringBones.Log("Spring chain '%s': unknown bone '%s'", proto.name.c_str(), root_name.asString().c_str());
                    continue;
                }
                SpringChain chain = proto;
                chain.name = proto.name + "/" + root_name.asString();
                chain.bones.push_back(*root);
                while (child_count[chain.bones.back()] == 1)
                    chain.bones.push_back(first_child[chain.bones.back()]);
                if (chain.bones.size() < 2)
                {
                    LogSpringBones.Log("Spring chain '%s': needs at least two bones", chain.name.c_str());
                    continue;
                }
                // tip: the last bone continues as long as the previous one
                const uint32_t last = chain.bones.back(), before_last = chain.bones[chain.bones.size() - 2];
                chain.tip_offset = to_bone_space(bind_model[last], (bind_model[last].translation - bind_model[before_last].translation) * tip_length);

                const size_t n = chain.bones.size() + 1;
                chain.position.assign(n, glm::vec3(0.0f));
                chain.velocity.assign(n, glm::vec3(0.0f));
                chain.length.assign(n - 1, 0.0f);
                chains.push_back(std::move(chain));
            }
        }

        for (const Json::Value& c : (*json)["jiggles"])
        {
            SpringJiggle proto;
            proto.name = c["name"].asString();
            proto.enabled = c.get("enabled", true).asBool();
            proto.stiffness = c.get("stiffness", proto.stiffness).asFloat();
            proto.damping = c.get("damping", proto.damping).asFloat();
            proto.gravity = c.get("gravity", proto.gravity).asFloat();
            proto.max_offset = c.get("max_offset", proto.max_offset).asFloat();
            for (const Json::Value& bone_name : c["bones"])
            {
                const std::optional<uint32_t> bone = rig.find_bone(bone_name.asString());
                if (!bone)
                {
                    LogSpringBones.Log("Spring jiggle '%s': unknown bone '%s'", proto.name.c_str(), bone_name.asString().c_str());
                    continue;
                }
                SpringJiggle jiggle = proto;
                jiggle.name = proto.name + "/" + bone_name.asString();
                jiggle.bone = *bone;
                jiggles.push_back(std::move(jiggle));
            }
        }

        LogSpringBones.Log("Spring bones '%s': %zu chains, %zu jiggles, %zu colliders", config.c_str(), chains.size(), jiggles.size(), colliders.size());
        return !chains.empty() || !jiggles.empty();
    }

    void simulate_spring_bones(SpringBones& springs, const Rig& rig, BonePose& local, const glm::mat4& model_to_world, float dt)
    {
        thread_local BonePose model;
        rig.to_model(local, model);

        const glm::quat world_rotation = glm::normalize(glm::quat_cast(glm::mat3(
            glm::normalize(glm::vec3(model_to_world[0])), glm::normalize(glm::vec3(model_to_world[1])), glm::normalize(glm::vec3(model_to_world[2])))));
        const glm::quat world_rotation_inv = glm::inverse(world_rotation);
        const glm::mat4 world_to_model = glm::inverse(model_to_world);
        auto to_world = [&](const glm::vec3& p) { return glm::vec3(model_to_world * glm::vec4(p, 1.0f)); };

        const bool reset = springs.needs_reset;
        springs.needs_reset = false;

        for (SpringCollider& c : springs.colliders)
        {
            c.prev_a = c.a;
            c.prev_b = c.b;
            c.a = to_world(bone_point(model[c.bone], c.offset));
            c.b = c.capsule ? to_world(bone_point(model[c.to_bone], c.to_offset)) : c.a;
            if (reset)
            {
                c.prev_a = c.a;
                c.prev_b = c.b;
            }
        }

        const int32_t steps = dt > 1e-6f ? std::clamp((int32_t)std::ceil(dt * substep_rate), 1, std::max(springs.max_substeps, 1)) : 0;
        const float h = steps > 0 ? dt / (float)steps : 0.0f;

        thread_local std::vector<glm::vec3> anim, dir;
        for (SpringChain& chain : springs.chains)
        {
            if (!chain.enabled)
                continue;
            const size_t n = chain.position.size();   // bones + tip
            anim.resize(n);
            dir.resize(n - 1);
            for (size_t i = 0; i + 1 < n; ++i)
                anim[i] = to_world(model[chain.bones[i]].translation);
            anim[n - 1] = to_world(bone_point(model[chain.bones.back()], chain.tip_offset));
            for (size_t i = 0; i + 1 < n; ++i)
            {
                dir[i] = anim[i + 1] - anim[i];
                chain.length[i] = glm::length(dir[i]);
                dir[i] = chain.length[i] > 1e-6f ? dir[i] / chain.length[i] : glm::vec3(0, -1, 0);
            }

            const glm::vec3 root = anim[0];
            if (reset || glm::length(root - chain.prev_root) > teleport_distance)
            {
                chain.position = anim;
                std::ranges::fill(chain.velocity, glm::vec3(0.0f));
                chain.prev_root = root;
            }

            // part of the root's motion carries the whole chain (no lag, no inertia for that part)
            const glm::vec3 carried = (root - chain.prev_root) * chain.follow;
            for (size_t i = 1; i < n; ++i)
                chain.position[i] += carried;

            const float stiffness = chain.stiffness * springs.stiffness_scale;
            const glm::vec3 gravity(0.0f, -chain.gravity * springs.gravity_scale, 0.0f);
            // damping ratio -> rate: the same calm at any stiffness
            const float keep = std::exp(-2.0f * chain.damping * springs.damping_scale * std::sqrt(stiffness) * h);
            const float drag = chain.drag * springs.drag_scale;
            const glm::vec3 root_velocity = dt > 1e-6f ? (root - chain.prev_root) * (1.0f - chain.follow) / dt : glm::vec3(0.0f);
            const float max_angle = glm::radians(chain.max_angle);

            for (int32_t step = 0; step < steps; ++step)
            {
                const float f = (float)(step + 1) / (float)steps;
                chain.position[0] = glm::mix(chain.prev_root, root, f);

                // the animated direction of a segment, turned like its simulated parent
                glm::quat parent_delta(1, 0, 0, 0);
                for (size_t i = 1; i < n; ++i)
                {
                    const glm::vec3& p0 = chain.position[i - 1];
                    const float len = chain.length[i - 1];
                    const glm::vec3 target_dir = parent_delta * dir[i - 1];

                    const glm::vec3 previous = chain.position[i];
                    // velocities exclude the carried (`follow`) part of the root's motion
                    glm::vec3 v = glm::mix(root_velocity, chain.velocity[i], keep);
                    v += (gravity + (p0 + target_dir * len - previous) * stiffness - v * drag) * h;
                    glm::vec3 p = previous + v * h;

                    auto fix_length = [&] {
                        const glm::vec3 d = p - p0;
                        const float l = glm::length(d);
                        p = l > 1e-6f ? p0 + d * (len / l) : p0 + target_dir * len;
                    };
                    fix_length();

                    if (max_angle > 0.0f)
                    {
                        const glm::vec3 d = (p - p0) / std::max(len, 1e-6f);
                        const float angle = std::acos(std::clamp(glm::dot(d, target_dir), -1.0f, 1.0f));
                        if (angle > max_angle)
                        {
                            glm::vec3 axis = glm::cross(target_dir, d);
                            if (glm::dot(axis, axis) > 1e-10f)
                                p = p0 + glm::angleAxis(max_angle, glm::normalize(axis)) * target_dir * len;
                        }
                    }

                    const glm::vec3 free = p;
                    glm::vec3 contact_normal(0.0f);
                    for (int32_t iteration = 0; iteration < 2; ++iteration)
                    {
                        bool moved = false;
                        for (uint32_t ci : chain.colliders)
                        {
                            const SpringCollider& c = springs.colliders[ci];
                            const glm::vec3 a = glm::mix(c.prev_a, c.a, f);
                            const glm::vec3 b = glm::mix(c.prev_b, c.b, f);
                            const float min_distance = c.radius + chain.radius;
                            // a chain growing out of a collider (skirt from the hips) would be thrown around by it
                            if (distance_to_segment(p0, a, b) < c.radius)
                                continue;
                            glm::vec3 normal;
                            if (push_out(p0, p, a, b, min_distance, normal))
                            {
                                moved = true;
                                contact_normal += normal;
                            }
                        }
                        if (!moved)
                            break;
                        fix_length();
                    }

                    if (glm::dot(contact_normal, contact_normal) > 1e-10f)
                    {
                        // a contact moves the particle but does not throw it: keep the free velocity without the
                        // part into the collider (pushes turned into velocity made resting hair bounce forever)
                        const glm::vec3 n = glm::normalize(contact_normal);
                        glm::vec3 v_free = (free - previous) / h;
                        v_free -= n * std::min(glm::dot(v_free, n), 0.0f);
                        chain.velocity[i] = v_free;
                    }
                    else
                        chain.velocity[i] = (p - previous) / h;
                    chain.position[i] = p;
                    parent_delta = rotation_between(target_dir, p - p0) * parent_delta;
                }
            }
            chain.prev_root = root;
            chain.position[0] = root;

            // back to bones: each bone turns like its segment, children sit at the particles
            glm::quat delta(1, 0, 0, 0);
            for (size_t i = 0; i + 1 < n; ++i)
            {
                const uint32_t bone = chain.bones[i];
                delta = rotation_between(delta * dir[i], chain.position[i + 1] - chain.position[i]) * delta;
                model[bone].rotation = glm::normalize(world_rotation_inv * delta * world_rotation * model[bone].rotation);
                if (i > 0)
                    model[bone].translation = glm::vec3(world_to_model * glm::vec4(chain.position[i], 1.0f));
                const int32_t parent = rig.parents[bone];
                local[bone] = parent >= 0 ? relative(model[parent], model[bone]) : model[bone];
            }

            if (springs.draw)
                for (size_t i = 0; i + 1 < n; ++i)
                {
                    debug_draw::line(chain.position[i], chain.position[i + 1], glm::vec4(1.0f, 0.8f, 0.1f, 1.0f), { .depth_test = false });
                    debug_draw::sphere(chain.position[i + 1], chain.radius, glm::vec4(1.0f, 0.8f, 0.1f, 1.0f), { .depth_test = false });
                }
        }

        for (SpringJiggle& j : springs.jiggles)
        {
            if (!j.enabled)
                continue;
            const glm::vec3 target = to_world(model[j.bone].translation);
            if (reset || glm::length(target - j.prev_target) > teleport_distance)
            {
                j.position = j.prev_target = target;
                j.velocity = glm::vec3(0.0f);
            }
            const glm::vec3 target_velocity = dt > 1e-6f ? (target - j.prev_target) / dt : glm::vec3(0.0f);
            const float damping = 2.0f * j.damping * std::sqrt(j.stiffness);
            const glm::vec3 gravity(0.0f, -j.gravity, 0.0f);
            const float max_offset = j.max_offset * springs.jiggle_scale;

            for (int32_t step = 0; step < steps; ++step)
            {
                const glm::vec3 t = glm::mix(j.prev_target, target, (float)(step + 1) / (float)steps);
                // damping against the target's motion: walking at a steady speed leaves no lag
                j.velocity += ((t - j.position) * j.stiffness - (j.velocity - target_velocity) * damping + gravity) * h;
                j.position += j.velocity * h;

                glm::vec3 offset = j.position - t;
                const float len = glm::length(offset);
                if (max_offset <= 0.0f)
                {
                    j.position = t;
                    j.velocity = target_velocity;
                }
                else if (len > max_offset)
                {
                    // at the limit: stop instead of bouncing off it
                    const glm::vec3 n = offset / len;
                    j.position = t + n * max_offset;
                    j.velocity -= n * std::max(glm::dot(j.velocity - target_velocity, n), 0.0f);
                }
            }
            j.prev_target = target;

            model[j.bone].translation = glm::vec3(world_to_model * glm::vec4(j.position, 1.0f));
            const int32_t parent = rig.parents[j.bone];
            local[j.bone] = parent >= 0 ? relative(model[parent], model[j.bone]) : model[j.bone];

            if (springs.draw)
            {
                debug_draw::line(target, j.position, glm::vec4(1.0f, 0.3f, 0.6f, 1.0f), { .depth_test = false });
                debug_draw::sphere(j.position, 0.01f, glm::vec4(1.0f, 0.3f, 0.6f, 1.0f), { .depth_test = false });
            }
        }

        if (springs.draw)
            for (const SpringCollider& c : springs.colliders)
                draw_capsule(c.a, c.b, c.radius, glm::vec4(0.2f, 0.7f, 1.0f, 1.0f));
    }

    namespace
    {
        // after the controllers (and IK), before the output goes to the mesh
        [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<PostProcess>, =ecs::after<Evaluate>, =ecs::before<Apply>]]
        void simulate_springs(ecs::Query<SpringBones, Animator, const Transform> entities, ecs::Res<ecs::FrameTime> time)
        {
            PROFILE("spring bones");
            entities.each([&](SpringBones& springs, Animator& animator, const Transform& transform) {
                if (!animator.valid() || springs.config.empty())
                    return;
                if (!springs.loaded || springs.reload)
                {
                    springs.reload = false;
                    springs.load(*animator.rig);
                }
                if (!springs.enabled)
                {
                    springs.needs_reset = true;
                    return;
                }
                const float dt = animator.paused ? 0.0f : std::min((float)time->dt, 0.1f) * animator.time_scale;
                const glm::mat4 model_to_world = transform.matrix() * animator.rig->skeleton->root_transform;
                simulate_spring_bones(springs, *animator.rig, animator.output.bones, model_to_world, dt);
            });
        }

        ECS_REGISTER()
        SCENE_REGISTER_COMPONENTS(SpringBones)
    }
}
