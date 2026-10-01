module locomotion;

import std.compat;
import glm;
import assets;
import animation;
import physics;
import :anim_graph;

// The control rig of ALS (CR_Als) and the feet state of UAlsAnimationInstance (RefreshFeet, foot lock,
// RefreshDynamicTransitions), on the model space pose after the graph:
//
//   spine rotation   the view yaw while aiming, spread over the spine bones (RefreshSpineRotation)
//   diagonal scaling feet reach further on diagonal blends (RefreshDiagonalScaling)
//   foot lock        a planted foot stays where it was in the world while the body turns or slides
//   foot offset      the ground under each foot (a trace), the pelvis lowered to the lower foot
//   foot IK          two bone IK of the legs to the locked / offset feet, feet aligned to the ground
//
// The foot targets are the animated foot bones of this frame (ALS: the IK foot bones, or its virtual foot bones
// when bUseFootIkBones is off - the clips here keep the IK bones at the bind pose). Hand IK is not ported (only
// weapon overlays use it).
namespace loco
{
    namespace
    {
        constexpr float full_weight = 1.0f - 1e-5f;
        constexpr float relevant_weight = 1e-5f;

        // rotation about `up` that takes the horizontal part of `from` onto that of `to`, degrees
        float signed_angle_around(const glm::vec3& up, const glm::vec3& from, const glm::vec3& to)
        {
            const glm::vec3 f = from - up * glm::dot(from, up);
            const glm::vec3 t = to - up * glm::dot(to, up);
            if (glm::dot(f, f) < 1e-10f || glm::dot(t, t) < 1e-10f)
                return 0.0f;
            return std::atan2(glm::dot(up, glm::cross(f, t)), glm::dot(f, t)) * (180.0f / pi);
        }

        // twist of a rotation about `axis` (swing-twist decomposition), degrees
        float twist_angle(const glm::quat& q, const glm::vec3& axis)
        {
            const float projection = glm::dot(glm::vec3(q.x, q.y, q.z), axis);
            return unwind_degrees(2.0f * std::atan2(projection, q.w) * (180.0f / pi));
        }

        glm::quat around(const glm::vec3& axis, float degrees)
        {
            return glm::angleAxis(degrees * (pi / 180.0f), axis);
        }

        struct Space
        {
            glm::mat4 model_to_world;
            glm::mat4 world_to_model;
            glm::quat rotation;              // model -> world
            glm::vec3 up;                    // world up in model space

            glm::vec3 to_world(const glm::vec3& p) const { return glm::vec3(model_to_world * glm::vec4(p, 1.0f)); }
            glm::vec3 to_model(const glm::vec3& p) const { return glm::vec3(world_to_model * glm::vec4(p, 1.0f)); }
        };

        // ConstrainFootLock: the locked foot can't twist the leg into a spiral when the body turns quickly
        void constrain_foot_lock(const LocomotionAnimation& a, const AnimationSettings& s, const Space& space, FootState& foot)
        {
            const glm::vec3 thigh_axis = a.feet.pelvis_rotation * foot.thigh_axis_pelvis;
            const float thigh_delta = signed_angle_around(space.up, thigh_axis, foot.lock_location);
            if (std::abs(thigh_delta) > s.foot_lock_thigh_angle_limit + 1e-4f)
            {
                const float clamped = std::clamp(thigh_delta, -s.foot_lock_thigh_angle_limit, s.foot_lock_thigh_angle_limit);
                const glm::quat offset = around(space.up, clamped - thigh_delta);
                foot.lock_location = offset * foot.lock_location;
                foot.lock_rotation = glm::normalize(offset * foot.lock_rotation);
                foot.lock_location_world = space.to_world(foot.lock_location);
                foot.lock_rotation_world = glm::normalize(space.rotation * foot.lock_rotation);
            }

            const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
            const float foot_delta = twist_angle(glm::normalize(foot.lock_rotation_world * glm::inverse(foot.target_rotation)), world_up);
            if (std::abs(foot_delta) > s.foot_lock_foot_angle_limit + 1e-4f)
            {
                const float clamped = std::clamp(foot_delta, -s.foot_lock_foot_angle_limit, s.foot_lock_foot_angle_limit);
                foot.lock_rotation_world = glm::normalize(around(world_up, clamped - foot_delta) * foot.lock_rotation_world);
            }
        }

        // RefreshFootLock: a lock curve of 1 keeps the foot where it is in the world; lower values let it go
        // (never back in)
        void refresh_foot_lock(Context& x, const Space& space, FootState& foot, float ik_amount, float lock_amount)
        {
            LocomotionAnimation& a = x.a;
            const AnimFeetState& feet = a.feet;
            float new_amount = lock_amount;
            if (a.locomotion.moving_smooth || a.locomotion_mode != LocomotionMode::grounded)
            {
                // released smoothly while moving or in the air, whatever the curve says
                const float speed = a.locomotion.moving_smooth ? 5.0f : 0.6f;
                new_amount = feet.became_valid ? 0.0f : std::max(0.0f, std::min(new_amount, foot.lock_amount - x.g.dt * speed));
            }

            if (!x.s.allow_foot_lock || ik_amount * new_amount <= relevant_weight)
            {
                if (foot.lock_amount > 0.0f)
                {
                    foot.lock_amount = 0.0f;
                    foot.lock_location_world = foot.lock_location = glm::vec3(0.0f);
                    foot.lock_rotation_world = foot.lock_rotation = glm::quat(1, 0, 0, 0);
                }
                foot.final_location = space.to_model(foot.target_location);
                foot.final_rotation = glm::normalize(glm::inverse(space.rotation) * foot.target_rotation);
                return;
            }

            if (new_amount >= full_weight)
            {
                if (new_amount > foot.lock_amount)
                {
                    // locks where the foot was last frame (without IK); the target could make it jump
                    glm::vec3 location = feet.became_valid ? foot.target_location : space.to_world(foot.final_location);
                    glm::quat rotation = feet.became_valid ? foot.target_rotation : glm::normalize(space.rotation * foot.final_rotation);
                    // close to locked already: keep the old lock (no foot "teleportation")
                    if (foot.lock_amount <= 0.9f)
                    {
                        foot.lock_location_world = location;
                        foot.lock_rotation_world = rotation;
                    }
                }
                foot.lock_amount = 1.0f;
            }
            else if (new_amount <= foot.lock_amount)
                foot.lock_amount = new_amount;

            foot.lock_location = space.to_model(foot.lock_location_world);
            foot.lock_rotation = glm::normalize(glm::inverse(space.rotation) * foot.lock_rotation_world);
            constrain_foot_lock(a, x.s, space, foot);

            const glm::vec3 location = glm::mix(foot.target_location, foot.lock_location_world, foot.lock_amount);
            const glm::quat rotation = anim::nlerp(foot.target_rotation, foot.lock_rotation_world, foot.lock_amount);
            foot.final_location = space.to_model(location);
            foot.final_rotation = glm::normalize(glm::inverse(space.rotation) * rotation);
        }

        // TraceFootOffset: the walkable ground under the foot, as a height in model space (+ the extra height an
        // ankle needs on a slope)
        void trace_foot_offset(Context& x, const Space& space, FootState& foot, float ik_amount)
        {
            foot.offset_z = 0.0f;
            foot.offset_normal = space.up;
            if (ik_amount < 1e-4f)
                return;
            const LocomotionAnimation& a = x.a;
            // from above to below the ground plane of the character, at the foot
            const glm::vec3 horizontal = foot.final_location - space.up * glm::dot(foot.final_location, space.up);
            const glm::vec3 start = space.to_world(horizontal + space.up * als_to_m(a, 50.0f));
            const glm::vec3 end = space.to_world(horizontal - space.up * als_to_m(a, 80.0f));
            const float length = glm::length(end - start);
            if (length < 1e-4f)
                return;
            const phys::QueryFilter filter{
                .categories = phys::Category::static_world | phys::Category::dynamic | phys::Category::voxel,
                .ignore_bodies = std::span(&x.character_body, 1),
            };
            const std::optional<phys::Hit> hit = x.physics.raycast(start, (end - start) / length, length, filter);
            if (!hit)
                return;
            const glm::vec3 normal = glm::normalize(glm::inverse(space.rotation) * hit->normal);
            constexpr float walkable_floor_cos = 0.7071f;   // 45 degrees
            if (glm::dot(normal, space.up) < walkable_floor_cos)
                return;
            const float slope_cos = glm::dot(normal, space.up);
            const float foot_height = a.feet.foot_height;
            const float slope_offset = slope_cos > 1e-4f ? foot_height / slope_cos - foot_height : 0.0f;
            foot.offset_z = glm::dot(space.to_model(hit->position), space.up) + slope_offset;
            foot.offset_normal = normal;
        }

        // ApplyFootIk: offset and lock the foot (spring), bend the leg to it, align the foot with the ground
        void apply_foot_ik(Context& x, const Space& space, anim::Pose& pose, BonePose& model, int side, float weight)
        {
            LocomotionAnimation& a = x.a;
            AnimFeetState& feet = a.feet;
            FootState& foot = side == 0 ? feet.left : feet.right;
            const uint32_t thigh = (uint32_t)feet.bones.thigh[side], calf = (uint32_t)feet.bones.calf[side], foot_bone = (uint32_t)feet.bones.foot[side];
            const uint32_t pelvis = (uint32_t)feet.bones.pelvis;
            const float moving = pose.curves[curve::PoseMoving];
            const float dt = x.g.dt;

            // ApplyFootOffsetLocation: the foot no higher than the pelvis allows, not below the pelvis offset
            const glm::vec3 target = foot.final_location;
            const float pelvis_height = glm::dot(model[pelvis].translation, space.up);
            const float max_foot_height = pelvis_height - als_to_m(a, glm::mix(20.0f, 50.0f, moving));
            float target_offset = std::min(foot.offset_z, max_foot_height - glm::dot(target, space.up));
            target_offset = std::max(target_offset, feet.pelvis_offset);
            if (!foot.ik_initialized)
            {
                foot.ik_initialized = true;
                foot.ik_offset_z = target_offset;
                foot.ik_offset_velocity = 0.0f;
            }
            else
                anim::spring_damper(foot.ik_offset_z, foot.ik_offset_velocity, target_offset, 0.0f, dt, 12.0f, 2.0f);
            glm::vec3 foot_location = target + space.up * foot.ik_offset_z;
            // never fully straight
            const glm::vec3 thigh_location = model[thigh].translation;
            const glm::vec3 leg = foot_location - thigh_location;
            const float max_leg = feet.leg_length * 0.99f;
            if (glm::length(leg) > max_leg && max_leg > 0.0f)
                foot_location = thigh_location + glm::normalize(leg) * max_leg;

            // the knee towards where it points now (smoothed)
            const glm::vec3 a_location = model[thigh].translation, b_location = model[calf].translation, c_location = model[foot_bone].translation;
            glm::vec3 pole_direction = b_location - a_location;
            const glm::vec3 ac = c_location - a_location;
            if (glm::dot(ac, ac) > 1e-10f)
            {
                const glm::vec3 axis = glm::normalize(ac);
                const glm::vec3 projection = a_location + axis * glm::dot(b_location - a_location, axis);
                pole_direction = b_location - projection;
            }
            if (glm::dot(pole_direction, pole_direction) > 1e-10f)
                pole_direction = glm::normalize(pole_direction);
            const glm::vec3 pole_target = b_location + pole_direction * als_to_m(a, 40.0f);
            if (!foot.pole_initialized)
            {
                foot.pole_initialized = true;
                foot.pole = pole_target;
            }
            else
                foot.pole = glm::mix(foot.pole, pole_target, damper_exact_alpha(dt, 0.05f));

            anim::two_bone_ik(x.g.rig, model, thigh, calf, foot_bone, foot_location, foot.pole, weight);

            // ApplyFootOffsetRotation: the foot target rotation tilted onto the ground normal (less while moving:
            // ALS limits the swing of the foot in calf space, to nothing while moving)
            if (!foot.rotation_initialized)
            {
                foot.rotation_initialized = true;
                foot.damped_normal = foot.offset_normal;
            }
            else
                foot.damped_normal = glm::normalize(glm::mix(foot.damped_normal, foot.offset_normal, damper_exact_alpha(dt, 0.1f)));
            glm::quat offset = anim::rotation_between(space.up, foot.damped_normal);
            const float max_tilt = glm::mix(30.0f, 10.0f, moving) * (pi / 180.0f);
            const float tilt = 2.0f * std::acos(std::clamp(std::abs(offset.w), 0.0f, 1.0f));
            if (tilt > max_tilt)
                offset = anim::scale_rotation(offset, max_tilt / tilt);
            thread_local BonePose before;
            before = model;
            model[foot_bone].rotation = anim::nlerp(model[foot_bone].rotation, glm::normalize(offset * foot.final_rotation), weight);
            const uint32_t changed[] = { foot_bone };
            anim::propagate_changes(x.g.rig, before, model, changed);
        }
    }

    void init_feet(LocomotionAnimation& a, const anim::Rig& rig)
    {
        AnimFeetState& f = a.feet;
        auto bone = [&](const char* name) { auto b = rig.find_bone(name); return b ? (int32_t)*b : -1; };
        f.bones.pelvis = bone("pelvis");
        const char* thighs[2] = { "thigh_l", "thigh_r" }, *calves[2] = { "calf_l", "calf_r" }, *feet[2] = { "foot_l", "foot_r" };
        for (int side = 0; side < 2; ++side)
        {
            f.bones.thigh[side] = bone(thighs[side]);
            f.bones.calf[side] = bone(calves[side]);
            f.bones.foot[side] = bone(feet[side]);
        }
        for (const char* name : { "spine_01", "spine_02", "spine_03" })
            if (int32_t b = bone(name); b >= 0)
                f.bones.spine.push_back((uint32_t)b);
        if (f.bones.pelvis < 0 || f.bones.thigh[0] < 0 || f.bones.calf[0] < 0 || f.bones.foot[0] < 0
            || f.bones.thigh[1] < 0 || f.bones.calf[1] < 0 || f.bones.foot[1] < 0)
            return;

        BonePose model;
        rig.to_model(rig.bind_pose, model);
        // thigh direction in pelvis space, leg length, ankle height (bind pose)
        for (int side = 0; side < 2; ++side)
        {
            FootState& foot = side == 0 ? f.left : f.right;
            const glm::vec3 t = rig.bind_pose[(uint32_t)f.bones.thigh[side]].translation;
            foot.thigh_axis_pelvis = glm::length(t) > 1e-6f ? glm::normalize(t) : glm::vec3(0.0f);
        }
        const glm::vec3 thigh = model[(uint32_t)f.bones.thigh[0]].translation;
        const glm::vec3 calf = model[(uint32_t)f.bones.calf[0]].translation;
        const glm::vec3 foot = model[(uint32_t)f.bones.foot[0]].translation;
        f.leg_length = glm::length(calf - thigh) + glm::length(foot - calf);
        f.foot_height = std::max(foot.y - model[rig.root].translation.y, 0.0f);
    }

    void apply_control_rig(Context& x, anim::Pose& pose, const glm::mat4& model_to_world)
    {
        LocomotionAnimation& a = x.a;
        AnimFeetState& feet = a.feet;
        const anim::Rig& rig = x.g.rig;
        if (feet.bones.pelvis < 0 || feet.bones.foot[0] < 0 || feet.bones.foot[1] < 0 || feet.leg_length <= 0.0f)
            return;

        Space space;
        space.model_to_world = model_to_world;
        space.world_to_model = glm::inverse(model_to_world);
        space.rotation = glm::normalize(glm::quat_cast(glm::mat3(model_to_world)));
        space.up = glm::normalize(glm::inverse(space.rotation) * glm::vec3(0.0f, 1.0f, 0.0f));

        // RefreshSpineRotation: the view yaw spread over the spine bones
        if (!feet.bones.spine.empty() && std::abs(a.spine.final_yaw_angle) > 1e-3f)
        {
            // ALS yaw turns right: about up by -yaw
            const glm::quat delta = around(space.up, -a.spine.final_yaw_angle / float(feet.bones.spine.size()));
            for (uint32_t bone : feet.bones.spine)
            {
                const int32_t parent = rig.parents[bone];
                const glm::quat parent_rotation = parent >= 0 ? anim::model_transform(rig, pose, (uint32_t)parent).rotation : glm::quat(1, 0, 0, 0);
                pose.bones[bone].rotation = glm::normalize(glm::inverse(parent_rotation) * delta * parent_rotation * pose.bones[bone].rotation);
            }
        }

        thread_local BonePose model;
        rig.to_model(pose.bones, model);
        feet.pelvis_rotation = model[(uint32_t)feet.bones.pelvis].rotation;

        // RefreshDiagonalScaling: on diagonal blends the feet reach further sideways and forward
        const float moving = pose.curves[curve::PoseMoving];
        const float diagonal = 1.0f - std::abs((a.grounded.forward + a.grounded.backward - 0.5f) * 2.0f);
        const float diagonal_scale = 1.0f + 0.4f * diagonal * clamp01(moving);

        // RefreshFeetOnGameThread / RefreshFeet: targets, foot lock
        const bool teleported = !feet.valid || glm::length(a.locomotion.position - feet.previous_position) > als_to_m(a, 50.0f);
        feet.became_valid = !feet.valid;
        feet.valid = true;
        feet.previous_position = a.locomotion.position;
        const glm::vec3 root = model[rig.root].translation;
        for (int side = 0; side < 2; ++side)
        {
            FootState& foot = side == 0 ? feet.left : feet.right;
            BoneTransform target = model[(uint32_t)feet.bones.foot[side]];
            glm::vec3 horizontal = target.translation - root;
            const glm::vec3 vertical = space.up * glm::dot(horizontal, space.up);
            target.translation = root + vertical + (horizontal - vertical) * diagonal_scale;
            foot.target_location = space.to_world(target.translation);
            foot.target_rotation = glm::normalize(space.rotation * target.rotation);

            const float ik_amount = clamp01(pose.curves[side == 0 ? curve::FootLeftIk : curve::FootRightIk]);
            const float lock_amount = clamp01(pose.curves[side == 0 ? curve::FootLeftLock : curve::FootRightLock]);
            // teleports keep the lock where it is relative to the character
            if (teleported && !feet.became_valid && ik_amount * foot.lock_amount > relevant_weight)
            {
                foot.lock_location_world = space.to_world(foot.lock_location);
                foot.lock_rotation_world = glm::normalize(space.rotation * foot.lock_rotation);
            }
            if (feet.became_valid && ik_amount * foot.lock_amount > relevant_weight)
            {
                foot.lock_location_world = foot.target_location;
                foot.lock_rotation_world = foot.target_rotation;
            }
            refresh_foot_lock(x, space, foot, ik_amount, lock_amount);
        }

        // RefreshFootOffset: the ground under the feet
        const float left_ik = pose.curves[curve::FootLeftIk], right_ik = pose.curves[curve::FootRightIk];
        trace_foot_offset(x, space, feet.left, left_ik);
        trace_foot_offset(x, space, feet.right, right_ik);

        // RefreshPelvisOffset: down to the lower foot (spring), on the ground or when about to land
        const float pelvis_amount = clamp01(a.pose.grounded + a.pose.in_air * a.in_air.ground_prediction);
        const float pelvis_target = std::clamp(std::min(feet.left.offset_z, feet.right.offset_z), als_to_m(a, -30.0f), als_to_m(a, 40.0f)) * pelvis_amount;
        if (!feet.pelvis_initialized)
        {
            feet.pelvis_initialized = true;
            feet.pelvis_spring = pelvis_target;
            feet.pelvis_spring_velocity = 0.0f;
        }
        else
            anim::spring_damper(feet.pelvis_spring, feet.pelvis_spring_velocity, pelvis_target, 0.0f, x.g.dt, 2.0f, 1.0f);
        feet.pelvis_offset = feet.pelvis_spring * pelvis_amount;
        if (std::abs(feet.pelvis_offset) > 1e-5f)
        {
            thread_local BonePose before;
            before = model;
            model[(uint32_t)feet.bones.pelvis].translation += space.up * feet.pelvis_offset;
            const uint32_t changed[] = { (uint32_t)feet.bones.pelvis };
            anim::propagate_changes(rig, before, model, changed);
        }

        // RefreshFootIk
        if (left_ik > 1e-4f)
            apply_foot_ik(x, space, pose, model, 0, std::min(left_ik, 1.0f));
        if (right_ik > 1e-4f)
            apply_foot_ik(x, space, pose, model, 1, std::min(right_ik, 1.0f));

        rig.to_local(model, pose.bones);
    }

    // RefreshDynamicTransitions: a locked foot too far from where the animation wants it steps there (an additive
    // step of that foot in the Transition slot)
    void refresh_dynamic_transitions(Context& x)
    {
        LocomotionAnimation& a = x.a;
        if (a.dynamic_transition_frame_delay > 0)
        {
            --a.dynamic_transition_frame_delay;
            return;
        }
        if (!a.transitions_allowed || !a.feet.valid)
            return;

        const float threshold = als_to_m(a, x.s.dynamic_transition_foot_lock_distance_threshold);
        const float left_distance = glm::length(a.feet.left.target_location - a.feet.left.lock_location_world);
        const float right_distance = glm::length(a.feet.right.target_location - a.feet.right.lock_location_world);
        const bool left = a.feet.left.lock_amount > relevant_weight && left_distance > threshold;
        const bool right = a.feet.right.lock_amount > relevant_weight && right_distance > threshold;
        if (!left && !right)
            return;

        // both: the foot further away
        const bool crouching = a.stance == Stance::crouching;
        const AnimationClipHandle& clip = !left || (right && left_distance < right_distance)
            ? (crouching ? x.k.crouch_dynamic_transition_right : x.k.stand_dynamic_transition_right)
            : (crouching ? x.k.crouch_dynamic_transition_left : x.k.stand_dynamic_transition_left);
        // the next ones wait a few frames for the graph to react
        a.dynamic_transition_frame_delay = 2;
        play_transition(x, clip, x.s.dynamic_transition_blend_duration, x.s.dynamic_transition_blend_duration, x.s.dynamic_transition_play_rate, 0.0f);
    }
}
