module locomotion;

import std.compat;
import glm;
import assets;
import ecs;
import framework;
import rhcomponents;
import animation;
import physics;
import log;
import fixed_string;
import :anim_graph;

#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogLocomotionAnimation, Log);

namespace loco
{
    AnimationClipHandle LocomotionClips::clip(const char* path)
    {
        AnimationClipHandle h = AssetManager::get().load_animation(std::string("animations/als/") + path + ".gltf");
        if (!h.is_valid())
        {
            LogLocomotionAnimation.Log("ALS clip missing: %s", path);
            ok = false;
        }
        return h;
    }

    std::shared_ptr<const anim::BlendSpace> LocomotionClips::space(const char* path)
    {
        auto s = anim::load_blend_space(std::string("animations/als/") + path + ".json");
        ok &= s != nullptr;
        return s;
    }

    bool LocomotionClips::load()
    {
        stand_pose = clip("Base/A_Als_Stand_Pose");
        crouch_pose = clip("Base/A_Als_Crouch_Pose");
        const char* dirs[6] = { "Forward", "Backward", "Left_Forward", "Left_Backward", "Right_Forward", "Right_Backward" };
        for (int i = 0; i < 6; ++i)
        {
            walk_run[i] = space(("Grounded/WalkRun/BS_Als_WalkRun_" + std::string(dirs[i])).c_str());
            walk[i] = clip(("Grounded/WalkRun/A_Als_Walk_" + std::string(dirs[i])).c_str());
            crouch_walk[i] = clip(("Grounded/Crouch/A_Als_Crouch_Walk_" + std::string(dirs[i])).c_str());
        }
        crouch_walk_pose = clip("Grounded/Crouch/A_Als_Crouch_Walk_Pose");
        lean = space("Grounded/Lean/BS_Als_Lean");
        sprint = clip("Grounded/Sprint/A_Als_Sprint");
        sprint_acceleration = clip("Grounded/Sprint/A_Als_Sprint_Acceleration");
        const char* acc[4] = { "Forward", "Backward", "Left", "Right" };
        for (int i = 0; i < 4; ++i)
            acceleration[i] = clip(("Grounded/Acceleration/A_Als_Acceleration_" + std::string(acc[i])).c_str());
        rotate_left = clip("RotateInPlace/A_Als_Rotate_90_Left");
        rotate_right = clip("RotateInPlace/A_Als_Rotate_90_Right");
        crouch_rotate_left = clip("RotateInPlace/A_Als_Crouch_Rotate_90_Left");
        crouch_rotate_right = clip("RotateInPlace/A_Als_Crouch_Rotate_90_Right");
        stand_to_crouch = clip("Transitions/A_Als_StandToCrouch");
        crouch_to_stand = clip("Transitions/A_Als_CrouchToStand");
        stand_transition_left = clip("Transitions/A_Als_Stand_Transition_Left");
        stand_transition_right = clip("Transitions/A_Als_Stand_Transition_Right");
        crouch_transition_left = clip("Transitions/A_Als_Crouch_Transition_Left");
        crouch_transition_right = clip("Transitions/A_Als_Crouch_Transition_Right");
        stand_dynamic_transition_left = clip("Transitions/A_Als_Stand_DynamicTransition_Left");
        stand_dynamic_transition_right = clip("Transitions/A_Als_Stand_DynamicTransition_Right");
        crouch_dynamic_transition_left = clip("Transitions/A_Als_Crouch_DynamicTransition_Left");
        crouch_dynamic_transition_right = clip("Transitions/A_Als_Crouch_DynamicTransition_Right");
        stop_left = clip("Transitions/A_Als_Stop_Left");
        stop_right = clip("Transitions/A_Als_Stop_Right");
        turn_90_left = clip("TurnInPlace/A_Als_Turn_90_Left");
        turn_90_right = clip("TurnInPlace/A_Als_Turn_90_Right");
        turn_180_left = clip("TurnInPlace/A_Als_Turn_180_Left");
        turn_180_right = clip("TurnInPlace/A_Als_Turn_180_Right");
        crouch_turn_90_left = clip("TurnInPlace/A_Als_Crouch_Turn_90_Left");
        crouch_turn_90_right = clip("TurnInPlace/A_Als_Crouch_Turn_90_Right");
        crouch_turn_180_left = clip("TurnInPlace/A_Als_Crouch_Turn_180_Left");
        crouch_turn_180_right = clip("TurnInPlace/A_Als_Crouch_Turn_180_Right");
        roll = clip("Actions/Roll/A_Als_Roll");

        fall = clip("Air/Fall/A_Als_Fall");
        fall_fast = clip("Air/Fall/A_Als_Fall_Fast");
        flail = clip("Air/Fall/A_Als_Flail");
        jump_walk_left = clip("Air/Jump/A_Als_Jump_Walk_Left");
        jump_walk_right = clip("Air/Jump/A_Als_Jump_Walk_Right");
        jump_run_left = clip("Air/Jump/A_Als_Jump_Run_Left");
        jump_run_right = clip("Air/Jump/A_Als_Jump_Run_Right");
        jump_loop = clip("Air/Jump/A_Als_Jump_Loop");
        land_light = clip("Air/Land/A_Als_Land_Light");
        land_heavy = clip("Air/Land/A_Als_Land_Heavy");
        land_light_additive = clip("Air/Land/A_Als_Land_Light_Additive");
        land_heavy_additive = clip("Air/Land/A_Als_Land_Heavy_Additive");
        air_lean = space("Air/Lean/BS_Als_Air_Lean");

        look = space("View/BS_Als_Look");
        // the overlays that are poses + secondary motion (AB_Als_Default, Feminine, Masculine)
        const std::pair<const char*, float> overlay_defs[] = { { "Default", 0.75f }, { "Feminine", 0.5f }, { "Masculine", 1.0f } };
        for (const auto& [name, idle_alpha] : overlay_defs)
            overlays.push_back({ name, clip(("Overlays/Other/A_Als_" + std::string(name) + "_Poses").c_str()), clip("Base/A_Als_Idle"), idle_alpha });

        // weapon overlays: the frames of their poses clips as AB_Als_Rifle / AB_Als_PistolTwoHanded use them
        auto rifle = std::make_shared<WeaponOverlay>(WeaponOverlay{
            .relaxed_stand = 0, .relaxed_walk = 1, .relaxed_sprint = -1, .relaxed_crouch = 6,
            .air = 9, .air_landing = 10,
            .ready_stand = 2, .ready_walk = 3, .ready_crouch = 7,
            .aim_stand = 4, .aim_walk = 5, .aim_crouch = 8,
            .aim_clip = clip("Overlays/Rifle/A_Als_Rifle_Aim"),
            .aim_crouch_clip = clip("Overlays/Rifle/A_Als_Rifle_Aim_Crouch"),
            .run_arms = clip("Overlays/Rifle/A_Als_Rifle_Run_Arms"),
            .sprint_arms = clip("Overlays/Rifle/A_Als_Rifle_Sprint_Arms"),
        });
        overlays.push_back({ "Rifle", clip("Overlays/Rifle/A_Als_Rifle_Poses"), clip("Base/A_Als_Idle"), 0.75f, rifle });

        auto pistol = std::make_shared<WeaponOverlay>(WeaponOverlay{
            .relaxed_stand = 0, .relaxed_walk = 1, .relaxed_sprint = 2, .relaxed_crouch = 7,
            .ready_stand = 3, .ready_walk = 4, .ready_crouch = 8,
            .aim_stand = 5, .aim_walk = 6, .aim_crouch = 9,
            .aim_clip = clip("Overlays/PistolTwoHanded/A_Als_PistolTwoHanded_Aim"),
            .aim_crouch_clip = clip("Overlays/PistolTwoHanded/A_Als_PistolTwoHanded_Aim_Crouch"),
        });
        overlays.push_back({ "PistolTwoHanded", clip("Overlays/PistolTwoHanded/A_Als_PistolTwoHanded_Poses"), clip("Base/A_Als_Idle"),
            0.75f, pistol });
        return ok;
    }

    // ---------------------------------------------------------------- shared by the graph parts (anim_graph)

    // actor space (x forward, y right) of a world vector
    glm::vec2 to_actor(const LocomotionAnimation& a, const glm::vec3& v)
    {
        const glm::vec3 forward = yaw_to_direction(a.locomotion.yaw);
        const glm::vec3 right = yaw_to_direction(a.locomotion.yaw + 90.0f);
        return glm::vec2(glm::dot(v, forward), glm::dot(v, right));
    }

    glm::vec2 acceleration_amount(const LocomotionAnimation& a)
    {
        const AnimLocomotionState& l = a.locomotion;
        const float max = glm::dot(l.acceleration, l.velocity) >= 0.0f ? l.max_acceleration : l.max_braking_deceleration;
        if (max <= 1e-4f)
            return glm::vec2(0.0f);
        glm::vec2 amount = to_actor(a, l.acceleration) / max;
        const float length = glm::length(amount);
        return length > 1.0f ? amount / length : amount;
    }

    void refresh_rotate_in_place(Context& x)
    {
        LocomotionAnimation& a = x.a;
        const bool allowed = a.rotation_mode == RotationMode::aiming || a.view_mode == ViewMode::first_person;
        if (a.locomotion.moving || !allowed)
            a.rotating_left = a.rotating_right = false;
        else
        {
            const bool first_person = a.view_mode == ViewMode::first_person && a.rotation_mode != RotationMode::aiming;
            const float threshold = first_person ? x.s.rotate_in_place_first_person_view_yaw_angle_threshold : x.s.rotate_in_place_view_yaw_angle_threshold;
            a.rotating_left = a.view.yaw_angle < -threshold;
            a.rotating_right = a.view.yaw_angle > threshold;
        }

        const float target = !a.rotating_left && !a.rotating_right ? x.s.rotate_in_place_play_rate.x
            : map_range_clamped(a.view.yaw_speed, x.s.rotate_in_place_reference_view_yaw_speed.x, x.s.rotate_in_place_reference_view_yaw_speed.y,
                x.s.rotate_in_place_play_rate.x, x.s.rotate_in_place_play_rate.y);
        a.rotate_in_place_play_rate = a.pending_update ? target
            : glm::mix(a.rotate_in_place_play_rate, target, damper_exact_alpha(x.g.dt, 0.15f));
    }

    void play_transition(Context& x, const AnimationClipHandle& clip, float blend_in, float blend_out, float play_rate,
        float start_time, bool from_standing_idle_only)
    {
        if (from_standing_idle_only && (x.a.locomotion.moving || x.a.stance != Stance::standing))
            return;
        x.animator.slots.play(anim::Montage::from_clip(clip, transition_slot, blend_in, blend_out, 0.0f),
            { .rate = play_rate, .start_time = start_time });
    }

    void play_transition_left(Context& x, float blend_in, float blend_out, float rate, float start)
    {
        play_transition(x, x.a.stance == Stance::crouching ? x.k.crouch_transition_left : x.k.stand_transition_left, blend_in, blend_out, rate, start);
    }

    void play_transition_right(Context& x, float blend_in, float blend_out, float rate, float start)
    {
        play_transition(x, x.a.stance == Stance::crouching ? x.k.crouch_transition_right : x.k.stand_transition_right, blend_in, blend_out, rate, start);
    }

    void stop_transition_and_turn_in_place(Context& x, float blend_out)
    {
        x.animator.slots.stop_slot(transition_slot, blend_out);
        x.animator.slots.stop_slot(turn_in_place_standing_slot, blend_out);
        x.animator.slots.stop_slot(turn_in_place_crouching_slot, blend_out);
    }

    namespace
    {
        using Standing = LocomotionAnimation::Standing;
        using Crouching = LocomotionAnimation::Crouching;
        using Grounded = LocomotionAnimation::Grounded;
        using Move = Standing::Move;

        // ---------------------------------------------------------------- UAlsAnimationInstance refresh functions

        // NativeUpdateAnimation (game thread part): copies of the character
        void refresh_from_character(LocomotionAnimation& a, const LocomotionCharacter& c, const AnimationSettings& s, float dt, float alpha)
        {
            a.locomotion_mode = c.locomotion_mode;
            a.rotation_mode = c.rotation_mode;
            a.view_mode = c.view_mode;
            a.stance = c.stance;
            a.gait = c.gait;
            // a new action forgets how the last one should end in grounded (the end of a roll does not: its notify
            // can come before the action ends)
            if (a.action != c.action && c.action != LocomotionAction::none)
                a.grounded_entry = GroundedEntryMode::none;
            a.action = c.action;

            // RefreshInAirOnGameThread: jumps are counters of the fixed ticks
            const bool jump_requested = c.jump_count != a.in_air.jump_count;
            a.in_air.jump_count = c.jump_count;
            a.in_air.jumped = !a.pending_update && (a.in_air.jumped || jump_requested);

            a.view.yaw_world = c.view.yaw;
            a.view.pitch_world = c.view.pitch;
            a.view.yaw_speed = c.view.yaw_speed;

            AnimLocomotionState& l = a.locomotion;
            l.has_input = c.locomotion.has_input;
            l.input_yaw = c.locomotion.input_yaw;
            l.scale = c.size_scale;
            l.speed = c.locomotion.speed;
            l.speed_cm = c.locomotion.speed * 100.0f / std::max(c.size_scale, 1e-3f);
            l.velocity = c.velocity;
            l.velocity_yaw = c.locomotion.velocity_yaw;
            l.acceleration = a.pending_update ? glm::vec3(0.0f) : c.acceleration;
            l.max_acceleration = c.max_acceleration;
            l.max_braking_deceleration = c.braking_deceleration;
            l.moving = c.locomotion.moving;
            l.moving_smooth = (c.locomotion.has_input && c.locomotion.has_velocity) || l.speed_cm > s.moving_smooth_speed_threshold;
            l.target_yaw = c.locomotion.target_yaw;

            l.position = glm::mix(c.previous_position, c.position, alpha);
            const float previous_yaw = l.yaw;
            l.yaw = lerp_angle(c.previous_yaw, c.yaw, alpha);
            l.yaw_velocity = !a.pending_update && dt > 1e-6f ? unwind_degrees(l.yaw - previous_yaw) / dt : 0.0f;
        }

        // RefreshPose / RefreshLayering-like reads of the last pose's curves, RefreshView, feet amounts, transitions
        void refresh_thread_safe(LocomotionAnimation& a, const anim::Animator& animator)
        {
            auto curve = [&](uint32_t i) { return animator.curve(i); };

            AnimPoseState& p = a.pose;
            p.grounded = curve(curve::PoseGrounded);
            p.in_air = curve(curve::PoseInAir);
            p.standing = curve(curve::PoseStanding);
            p.crouching = curve(curve::PoseCrouching);
            p.moving = curve(curve::PoseMoving);
            p.gait = std::clamp(curve(curve::PoseGait), 0.0f, 3.0f);
            p.gait_walking = clamp01(p.gait);
            p.gait_running = clamp01(p.gait - 1.0f);
            p.gait_sprinting = clamp01(p.gait - 2.0f);
            p.unweighted_gait = p.grounded > 1e-6f ? p.gait / p.grounded : p.gait;
            p.unweighted_walking = clamp01(p.unweighted_gait);
            p.unweighted_running = clamp01(p.unweighted_gait - 1.0f);
            p.unweighted_sprinting = clamp01(p.unweighted_gait - 2.0f);

            a.foot_planted = std::clamp(curve(curve::FootPlanted), -1.0f, 1.0f);
            a.feet_crossing = clamp01(curve(curve::FeetCrossing));
            a.transitions_allowed = curve(curve::AllowTransitions) >= 1.0f - 1e-4f;
        }

        // RefreshGrounded: velocity blend and lean
        void refresh_grounded(Context& x)
        {
            LocomotionAnimation& a = x.a;
            AnimGroundedState& gs = a.grounded;
            const float dt = x.g.dt;

            glm::vec2 v = to_actor(a, a.locomotion.velocity);
            glm::vec2 target(0.0f);
            if (glm::length(v) > 1e-4f)
            {
                v = glm::normalize(v);
                target = v / (std::abs(v.x) + std::abs(v.y));
            }
            const float f = clamp01(target.x), b = std::abs(std::clamp(target.x, -1.0f, 0.0f));
            const float l = std::abs(std::clamp(target.y, -1.0f, 0.0f)), r = clamp01(target.y);
            if (gs.velocity_blend_initialization_required || x.s.velocity_blend_interpolation_half_life <= 0.0f)
            {
                gs.velocity_blend_initialization_required = false;
                gs.forward = f; gs.backward = b; gs.left = l; gs.right = r;
            }
            else
            {
                const float t = damper_exact_alpha(dt, x.s.velocity_blend_interpolation_half_life);
                gs.forward = glm::mix(gs.forward, f, t);
                gs.backward = glm::mix(gs.backward, b, t);
                gs.left = glm::mix(gs.left, l, t);
                gs.right = glm::mix(gs.right, r, t);
            }

            const glm::vec2 lean = acceleration_amount(a);
            if (a.pending_update || x.s.lean_interpolation_half_life <= 0.0f)
            {
                a.lean_right = lean.y;
                a.lean_forward = lean.x;
            }
            else
            {
                const float t = damper_exact_alpha(dt, x.s.lean_interpolation_half_life);
                a.lean_right = glm::mix(a.lean_right, lean.y, t);
                a.lean_forward = glm::mix(a.lean_forward, lean.x, t);
            }
        }

        // RefreshGroundedMovement: hips direction lock, movement direction, rotation yaw offsets
        void refresh_grounded_movement(Context& x)
        {
            LocomotionAnimation& a = x.a;
            AnimGroundedState& gs = a.grounded;
            gs.hips_direction_lock = std::clamp(x.animator.curve(curve::HipsDirectionLock), -1.0f, 1.0f);

            const float velocity_yaw_view_space = unwind_degrees(a.locomotion.velocity_yaw - a.view.yaw_world);

            if (a.rotation_mode == RotationMode::velocity_direction || a.gait == Gait::sprinting)
                gs.movement_direction = MovementDirection::forward;
            else
            {
                // UAlsMath::CalculateMovementDirection(angle, 70, 5)
                const float angle = velocity_yaw_view_space, half = 70.0f, threshold = 5.0f;
                if (angle >= -half - threshold && angle <= half + threshold)
                    gs.movement_direction = MovementDirection::forward;
                else if (angle >= half - threshold && angle <= 180.0f - half + threshold)
                    gs.movement_direction = MovementDirection::right;
                else if (angle <= -(half - threshold) && angle >= -(180.0f - half + threshold))
                    gs.movement_direction = MovementDirection::left;
                else
                    gs.movement_direction = MovementDirection::backward;
            }

            gs.yaw_offset_forward = eval(x.s.rotation_yaw_offset_forward, velocity_yaw_view_space);
            gs.yaw_offset_backward = eval(x.s.rotation_yaw_offset_backward, velocity_yaw_view_space);
            gs.yaw_offset_left = eval(x.s.rotation_yaw_offset_left, velocity_yaw_view_space);
            gs.yaw_offset_right = eval(x.s.rotation_yaw_offset_right, velocity_yaw_view_space);
        }

        void refresh_standing_movement(Context& x)
        {
            LocomotionAnimation& a = x.a;
            AnimStandingState& st = a.standing;
            const float speed = a.locomotion.speed_cm;

            st.stride_blend = glm::mix(eval(x.s.stride_blend_walk, speed, 1.0f), eval(x.s.stride_blend_run, speed, 1.0f), a.pose.unweighted_running);
            st.walk_run_blend = a.gait == Gait::walking ? 0.0f : 1.0f;

            const float walk_run = glm::mix(speed / x.s.animated_walk_speed, speed / x.s.animated_run_speed, a.pose.unweighted_running);
            const float walk_run_sprint = glm::mix(walk_run, speed / x.s.animated_sprint_speed, a.pose.unweighted_sprinting);
            st.play_rate = std::clamp(walk_run_sprint / std::max(st.stride_blend, 1e-3f), 1e-4f, 3.0f);
            st.sprint_block = clamp01(x.animator.curve(curve::SprintBlock));

            if (a.gait != Gait::sprinting)
            {
                st.sprint_time = 0.0f;
                st.sprint_acceleration = 0.0f;
                return;
            }
            // the acceleration animation only plays at the beginning of a sprint
            st.sprint_time = a.pending_update ? 0.5f : st.sprint_time + x.g.dt;
            st.sprint_acceleration = st.sprint_time >= 0.5f ? 0.0f : acceleration_amount(a).x;
        }

        void refresh_crouching_movement(Context& x)
        {
            LocomotionAnimation& a = x.a;
            const float speed = a.locomotion.speed_cm;
            a.crouching.stride_blend = eval(x.s.crouching_stride_blend, speed, 1.0f);
            a.crouching.play_rate = std::clamp(speed / (x.s.animated_crouch_speed * std::max(a.crouching.stride_blend, 1e-3f)), 1e-4f, 2.0f);
        }

        // ---- montages of the Transition and turn in place slots

        void refresh_turn_in_place(Context& x)
        {
            LocomotionAnimation& a = x.a;
            const bool allowed = a.rotation_mode == RotationMode::view_direction && a.view_mode != ViewMode::first_person;
            if (!a.transitions_allowed || !allowed)
            {
                a.turn_in_place_activation_delay = 0.0f;
                return;
            }
            // the view must stay turned away for a moment (shorter the further it is)
            if (a.view.yaw_speed >= x.s.turn_in_place_view_yaw_speed_threshold || std::abs(a.view.yaw_angle) <= x.s.turn_in_place_view_yaw_angle_threshold)
            {
                a.turn_in_place_activation_delay = 0.0f;
                return;
            }
            a.turn_in_place_activation_delay += x.g.dt;
            const float delay = map_range_clamped(std::abs(a.view.yaw_angle), x.s.turn_in_place_view_yaw_angle_threshold, 180.0f,
                x.s.turn_in_place_view_yaw_angle_to_activation_delay.x, x.s.turn_in_place_view_yaw_angle_to_activation_delay.y);
            if (a.turn_in_place_activation_delay <= delay)
                return;

            const bool left = remap_angle_for_counter_clockwise_rotation(a.view.yaw_angle) <= 0.0f;
            const bool turn_180 = std::abs(a.view.yaw_angle) >= x.s.turn_in_place_turn_180_angle_threshold;
            const bool standing = a.stance == Stance::standing;
            const LocomotionClips& k = x.k;
            const AnimationClipHandle& clip = standing
                ? (turn_180 ? (left ? k.turn_180_left : k.turn_180_right) : (left ? k.turn_90_left : k.turn_90_right))
                : (turn_180 ? (left ? k.crouch_turn_180_left : k.crouch_turn_180_right) : (left ? k.crouch_turn_90_left : k.crouch_turn_90_right));
            const float animated_angle = turn_180 ? 180.0f : 90.0f;

            // blending out by inertialization: a fading turn would keep its RotationYawSpeed curve and the character turning
            const float blend = x.s.turn_in_place_blend_duration;
            x.animator.slots.play(anim::Montage::from_clip(clip, standing ? turn_in_place_standing_slot : turn_in_place_crouching_slot, blend, blend, 0.0f),
                { .rate = x.s.turn_in_place_play_rate, .inertial_blend_out = true });

            if (debug_log_enabled())
                LogLocomotionAnimation.Log("Turn in place: %s %s, view yaw angle %.1f", clip.get().name.c_str(), standing ? "standing" : "crouching",
                    a.view.yaw_angle);

            // the RotationYawSpeed curve is scaled by this in the graph (play rate and turn angle)
            a.turn_in_place_play_rate = x.s.turn_in_place_play_rate;
            if (standing)   // crouching turns are not scaled by the angle (AIS_Als_Default)
                a.turn_in_place_play_rate *= std::abs(a.view.yaw_angle / animated_angle);
            a.turn_in_place_activation_delay = 0.0f;
        }


        // ---------------------------------------------------------------- AB_Als_Standing

        HipsDirection hips_of(Move m)
        {
            switch (m)
            {
            case Move::forward: return HipsDirection::forward;
            case Move::backward: return HipsDirection::backward;
            case Move::right_forward: return HipsDirection::right_forward;
            case Move::right_backward: return HipsDirection::right_backward;
            case Move::left_forward: return HipsDirection::left_forward;
            case Move::left_backward: return HipsDirection::left_backward;
            }
            return HipsDirection::forward;
        }

        // Movement States of standing and crouching: directional blends switched by movement direction and the hips
        // direction lock curve. `pivot` / `hips` are the crossfades of standing (crouching uses `direction` for all).
        void update_movement_states(LocomotionAnimation& a, anim::StateMachine<Move>& sm, const anim::Transition& direction,
            const anim::Transition& pivot, const anim::Transition& hips)
        {
            const AnimGroundedState& gs = a.grounded;
            const bool f = gs.movement_direction == MovementDirection::forward;
            const bool b = gs.movement_direction == MovementDirection::backward;
            const bool l = gs.movement_direction == MovementDirection::left;
            const bool r = gs.movement_direction == MovementDirection::right;
            const bool crossing_free = a.feet_crossing <= 0.0f;
            const bool hips_left = gs.hips_direction_lock <= -0.5f && crossing_free;
            const bool hips_right = gs.hips_direction_lock >= 0.5f && crossing_free;
            const bool hips_free = std::abs(gs.hips_direction_lock) < 0.5f && crossing_free && sm.current_weight() >= 1.0f;

            auto go = [&](Move to, const anim::Transition& t) {
                sm.transition(to, t);
                a.grounded.hips_direction = hips_of(to);
                return true;
            };
            switch (sm.state())
            {
            case Move::forward:
                (b && go(Move::backward, pivot)) || (r && go(Move::right_forward, direction)) || (l && go(Move::left_forward, direction));
                break;
            case Move::backward:
                (f && go(Move::forward, pivot)) || (r && go(Move::right_backward, direction)) || (l && go(Move::left_backward, direction));
                break;
            case Move::right_forward:
                (f && go(Move::forward, direction)) || (l && go(Move::left_backward, pivot)) || (b && go(Move::right_backward, direction))
                    || (hips_left && go(Move::right_backward, hips));
                break;
            case Move::left_forward:
                (f && go(Move::forward, direction)) || (b && go(Move::left_backward, direction)) || (r && go(Move::right_backward, pivot))
                    || (hips_right && go(Move::left_backward, hips));
                break;
            case Move::right_backward:
                (b && go(Move::backward, direction)) || (l && go(Move::left_forward, pivot)) || (f && go(Move::right_forward, hips))
                    || (hips_free && go(Move::right_forward, hips)) || (hips_right && go(Move::right_forward, hips));
                break;
            case Move::left_backward:
                (b && go(Move::backward, direction)) || (r && go(Move::right_forward, pivot)) || (f && go(Move::left_forward, direction))
                    || (hips_free && go(Move::left_forward, hips)) || (hips_left && go(Move::left_forward, hips));
                break;
            }
        }

        // poses of a movement state from the 6 directional caches: F, B, LF, LB, RF, RB
        template <typename F>
        anim::PoseRef movement_state_pose(Context& x, Move state, F&& direction)
        {
            const AnimGroundedState& gs = x.a.grounded;
            const float weights[4] = { gs.forward, gs.backward, gs.left, gs.right };
            int left = 2, right = 4;          // LF, RF
            float yaw_offset = gs.yaw_offset_forward;
            switch (state)
            {
            case Move::forward: left = 2; right = 4; yaw_offset = gs.yaw_offset_forward; break;
            case Move::backward: left = 3; right = 5; yaw_offset = gs.yaw_offset_backward; break;
            case Move::right_forward: left = 3; right = 4; yaw_offset = gs.yaw_offset_right; break;
            case Move::right_backward: left = 2; right = 5; yaw_offset = gs.yaw_offset_right; break;
            case Move::left_forward: left = 2; right = 5; yaw_offset = gs.yaw_offset_left; break;
            case Move::left_backward: left = 3; right = 4; yaw_offset = gs.yaw_offset_left; break;
            }
            const int children[4] = { 0, 1, left, right };
            anim::PoseRef pose = x.g.blend_n(std::span(weights), [&](uint32_t i) { return direction(children[i]); });
            set_curve(pose, curve::RotationYawOffset, yaw_offset);
            return pose;
        }

        anim::PoseRef standing_pose(Context& x)
        {
            anim::Graph& g = x.g;
            LocomotionAnimation& a = x.a;
            Standing& n = a.standing_nodes;
            const LocomotionClips& k = x.k;

            refresh_rotate_in_place(x);

            // ---- directional cycles, cached: each is evaluated once and shared by the states that use it
            Cache directions[6];
            Cache forward_base;
            const glm::vec2 cycle_input(a.standing.stride_blend, a.standing.walk_run_blend);
            const anim::PlayParams cycle{ .rate = a.standing.play_rate, .start = 0.2f, .sync = "Movement" };
            anim::BlendSpacePlayer* players[6] = { &n.forward, &n.backward, &n.left_forward, &n.left_backward, &n.right_forward, &n.right_backward };

            auto forward_base_pose = [&] {
                return cached(g, forward_base, [&] { return g.play(n.forward, *k.walk_run[0], cycle_input, cycle); });
            };
            auto direction = [&](int i) {
                return cached(g, directions[i], [&] {
                    if (i != 0)
                        return g.play(*players[i], *k.walk_run[i], cycle_input, cycle);
                    // forward: sprinting (with its acceleration at the start) when not blocked by landings
                    return g.blend(a.standing.sprint_block,
                        [&] {
                            return g.blend_list(n.sprint_list, a.gait == Gait::sprinting ? 1 : 0, 2, a.gait == Gait::sprinting ? 0.2f : 0.3f,
                                [&](uint32_t s) {
                                    if (s == 0)
                                        return forward_base_pose();
                                    return g.blend(n.sprint_acceleration_blend, n.sprint_acceleration_alpha.update(a.standing.sprint_acceleration, g.dt),
                                        [&] { return g.play(n.sprint, k.sprint, { .rate = a.standing.play_rate, .start = 0.1667f, .sync = "Movement" }); },
                                        [&] { return g.play(n.sprint_acceleration, k.sprint_acceleration,
                                            { .rate = a.standing.play_rate / 0.833333f, .start = 0.1667f, .sync = "Movement" }); });
                                }, anim::BlendCurve::cubic);
                        },
                        forward_base_pose);
                });
            };

            // ---- Movement: directional state machine + lean
            Cache movement;
            auto movement_pose = [&] {
                return cached(g, movement, [&] {
                    enter_on_relevance(g, n.movement_relevance, n.movement, Move::forward);
                    update_movement_states(a, n.movement,
                        crossfade(0.7f, anim::BlendCurve::cubic, &a.move_direction_change_profile),
                        crossfade(0.5f, anim::BlendCurve::cubic, &a.move_direction_change_profile),
                        crossfade(0.75f, anim::BlendCurve::quadratic_in_out, &a.move_direction_change_profile));
                    anim::PoseRef pose = g.state_machine(n.movement, [&](Move m) { return movement_state_pose(x, m, direction); });
                    const float lean_alpha = n.lean_alpha.update(a.pose.unweighted_running, g.dt);
                    pose = g.additive(std::move(pose), AdditiveKind::local, lean_alpha,
                        [&] { return g.evaluate(n.lean, *k.lean, glm::vec2(a.lean_right, a.lean_forward), 0.0f); });
                    set_curve(pose, curve::PoseMoving, 1.0f);
                    return pose;
                });
            };

            // ---- Movement Details: walk / run / run starts / pivots
            Cache details;
            auto details_pose = [&] {
                return cached(g, details, [&] {
                    refresh_grounded_movement(x);
                    if (g.becoming_relevant(n.standing_movement_relevance))
                    {
                        a.standing.sprint_time = 0.0f;
                        a.standing.pivot_active = false;
                    }
                    refresh_standing_movement(x);

                    using Detail = Standing::Detail;
                    enter_on_relevance(g, n.details_relevance, n.details, Detail::walk);
                    const bool running = a.gait == Gait::running || a.gait == Gait::sprinting;
                    const bool walking = a.gait == Gait::walking;
                    switch (n.details.state())
                    {
                    case Detail::walk:
                        if (running && a.pose.grounded < 1.0f)
                            n.details.transition(Detail::run, crossfade(0.2f));
                        else if (running && n.details_weight < 1.0f)
                            n.details.transition(Detail::run_start, inertial(0.1f));
                        else if (running)
                            n.details.transition(Detail::run_start_from_walk, inertial(0.1f));
                        break;
                    case Detail::run:
                        if (walking && a.pose.unweighted_running < 0.2f)
                            n.details.transition(Detail::walk, crossfade(0.1f));
                        else if (a.standing.pivot_active)
                            n.details.transition(Detail::first_pivot, inertial(0.1f));
                        break;
                    case Detail::run_start:
                        if (finished(n.run_start[0]))
                            n.details.transition(Detail::run, crossfade(0.2f));
                        break;
                    case Detail::run_start_from_walk:
                        if (finished(n.run_start_from_walk[0]))
                            n.details.transition(Detail::run, crossfade(0.1f));
                        break;
                    case Detail::first_pivot:
                        if (finished(n.first_pivot[0]))
                            n.details.transition(Detail::run, inertial(0.2f));
                        else if (a.standing.pivot_active)
                            n.details.transition(Detail::second_pivot, inertial(0.1f));
                        break;
                    case Detail::second_pivot:
                        if (a.standing.pivot_active)
                            n.details.transition(Detail::first_pivot, inertial(0.1f));
                        else if (finished(n.second_pivot[0]))
                            n.details.transition(Detail::run, inertial(0.2f));
                        break;
                    }
                    if (n.details.just_entered(g.frame) && (n.details.in(Detail::first_pivot) || n.details.in(Detail::second_pivot)))
                        a.standing.pivot_active = false;   // ResetPivot
                    n.details_weight = g.weight();

                    // the acceleration additives over the movement, by velocity direction
                    auto accelerated = [&](std::array<anim::SequencePlayer, 4>& players, float start, std::string_view sync) {
                        anim::PoseRef base = movement_pose();
                        return g.additive(std::move(base), AdditiveKind::local, 1.0f, [&] {
                            const AnimGroundedState& gs = a.grounded;
                            const float w[4] = { gs.forward, gs.backward, gs.left, gs.right };
                            return g.blend_n(std::span(w), [&](uint32_t i) {
                                return g.play(players[i], k.acceleration[i], { .rate = 1.25f, .loop = false, .start = start, .sync = sync });
                            });
                        });
                    };
                    return g.inertialize(n.details_inertialization, [&] {
                        return g.state_machine(n.details, [&](Detail d) {
                            switch (d)
                            {
                            case Detail::run_start: return accelerated(n.run_start, 0.15f, "Run Start");
                            case Detail::run_start_from_walk: return accelerated(n.run_start_from_walk, 0.1f, "Run Start");
                            case Detail::first_pivot: return accelerated(n.first_pivot, 0.25f, "First Pivot");
                            case Detail::second_pivot: return accelerated(n.second_pivot, 0.25f, "Second Pivot");
                            case Detail::walk:
                            case Detail::run: break;
                            }
                            return movement_pose();
                        });
                    });
                });
            };

            // ---- Stop: lock the planted foot, or plant the foot about to land, and play the additive stop
            using Stop = Standing::Stop;
            auto stop_pose = [&] {
                enter_on_relevance(g, n.stop_relevance, n.stop, Stop::entry);
                if (n.stop.in(Stop::entry))
                {
                    if (a.foot_planted <= -0.5f)
                        n.stop.transition(Stop::lock_left, crossfade(0.0f));
                    else if (a.foot_planted > 0.5f)
                        n.stop.transition(Stop::lock_right, crossfade(0.0f));
                    else if (a.foot_planted <= 0.0f)
                        n.stop.transition(Stop::plant_left, crossfade(0.1f));
                    else
                        n.stop.transition(Stop::plant_right, crossfade(0.1f));
                    if (n.stop.just_entered(g.frame))
                        play_transition(x, n.stop.in(Stop::lock_left) || n.stop.in(Stop::plant_left) ? k.stop_left : k.stop_right,
                            0.2f, 0.2f, 1.5f, 0.4f);
                }
                // walk frames where the foot is planted, by velocity direction (side poses by hips direction)
                auto planted = [&](bool left_foot) {
                    const AnimGroundedState& gs = a.grounded;
                    const float w[4] = { gs.forward, gs.backward, gs.left, gs.right };
                    const int f_frame = left_foot ? 4 : 21, b_frame = left_foot ? 4 : 21;
                    return g.blend_n(std::span(w), [&](uint32_t i) -> anim::PoseRef {
                        switch (i)
                        {
                        case 0: return g.evaluate_frame(k.walk[0], f_frame);
                        case 1: return g.evaluate_frame(k.walk[1], b_frame);
                        case 2:
                            return g.blend_list(n.plant_hips[left_foot ? 0 : 2], a.grounded.hips_direction == HipsDirection::left_backward ? 1 : 0, 2, 0.1f,
                                [&](uint32_t s) { return s == 0 ? g.evaluate_frame(k.walk[2], left_foot ? 6 : 24) : g.evaluate_frame(k.walk[3], left_foot ? 4 : 24); });
                        default:
                            return g.blend_list(n.plant_hips[left_foot ? 1 : 3], a.grounded.hips_direction == HipsDirection::right_backward ? 1 : 0, 2, 0.1f,
                                [&](uint32_t s) { return s == 0 ? g.evaluate_frame(k.walk[4], left_foot ? 7 : 23) : g.evaluate_frame(k.walk[5], left_foot ? 7 : 21); });
                        }
                    });
                };
                return g.state_machine(n.stop, [&](Stop s) {
                    anim::PoseRef pose = details_pose();
                    switch (s)
                    {
                    case Stop::entry: break;
                    case Stop::lock_left: set_curve(pose, curve::FootLeftLock, 1.0f); break;
                    case Stop::lock_right: set_curve(pose, curve::FootRightLock, 1.0f); break;
                    case Stop::plant_left:
                        pose = g.layered(std::move(pose), a.leg_left_mask, 1.0f, [&] { return planted(true); }, true);
                        set_curve(pose, curve::FootLeftLock, 1.0f);
                        break;
                    case Stop::plant_right:
                        pose = g.layered(std::move(pose), a.leg_right_mask, 1.0f, [&] { return planted(false); }, true);
                        set_curve(pose, curve::FootRightLock, 1.0f);
                        break;
                    }
                    return pose;
                });
            };

            // ---- Standing States
            using State = Standing::State;
            enter_on_relevance(g, n.states_relevance, n.states, State::idle);
            const bool moving = a.locomotion.moving_smooth;
            const State before = n.states.state();
            switch (n.states.state())
            {
            case State::idle:
                if (moving)
                    n.states.transition(State::move, crossfade(0.2f));
                else if (a.rotating_left)
                    n.states.transition(State::rotate_left, crossfade(0.2f));
                else if (a.rotating_right)
                    n.states.transition(State::rotate_right, crossfade(0.2f));
                break;
            case State::move:
                if (!moving && n.states.current_weight() >= 1.0f)
                    n.states.transition(State::stop, crossfade(0.0f));
                else if (!moving)
                    n.states.transition(State::idle, crossfade(0.2f));
                break;
            case State::stop:
                if (n.states.current_weight() >= 1.0f)
                    n.states.transition(State::idle, crossfade(0.3f));
                break;
            case State::rotate_left:
                if (finished(n.rotate_left) && !a.rotating_left)
                    n.states.transition(State::idle, crossfade(0.0f));
                else if (a.rotating_right)
                    n.states.transition(State::rotate_right, inertial(0.2f));
                else if (moving)
                    n.states.transition(State::idle, crossfade(0.2f));
                break;
            case State::rotate_right:
                if (finished(n.rotate_right) && !a.rotating_right)
                    n.states.transition(State::idle, crossfade(0.0f));
                else if (a.rotating_left)
                    n.states.transition(State::rotate_left, inertial(0.2f));
                else if (moving)
                    n.states.transition(State::idle, crossfade(0.2f));
                break;
            }
            // state entry / exit functions
            if (n.states.state() != before)
            {
                if (before == State::idle || n.states.in(State::move))
                    stop_transition_and_turn_in_place(x);
            }

            anim::PoseRef pose = g.inertialize(n.states_inertialization, [&] {
                return g.state_machine(n.states, [&](State s) -> anim::PoseRef {
                    switch (s)
                    {
                    case State::idle:
                    {
                        if (g.becoming_relevant(n.idle_relevance))
                            a.turn_in_place_activation_delay = 0.0f;
                        refresh_turn_in_place(x);
                        refresh_dynamic_transitions(x);
                        anim::PoseRef idle = g.evaluate(k.stand_pose, 0.0f);
                        set_curve(idle, curve::FootLeftLock, 1.0f);
                        set_curve(idle, curve::FootRightLock, 1.0f);
                        set_curve(idle, curve::AllowTransitions, 1.0f);
                        idle = g.slot(turn_in_place_standing_slot, std::move(idle));
                        scale_curve(idle, curve::RotationYawSpeed, a.turn_in_place_play_rate);
                        return idle;
                    }
                    case State::move: return details_pose();
                    case State::stop: return stop_pose();
                    case State::rotate_left:
                    case State::rotate_right:
                    {
                        const bool left = s == State::rotate_left;
                        anim::PoseRef rotate = g.play(left ? n.rotate_left : n.rotate_right, left ? k.rotate_left : k.rotate_right,
                            { .rate = a.rotate_in_place_play_rate, .loop = left ? a.rotating_left : a.rotating_right });
                        scale_curve(rotate, curve::RotationYawSpeed, a.rotate_in_place_play_rate);
                        return rotate;
                    }
                    }
                    return g.bind_pose();
                });
            });
            set_curve(pose, curve::PoseStanding, 1.0f);
            return pose;
        }


        // ---------------------------------------------------------------- AB_Als_Crouching

        anim::PoseRef crouching_pose(Context& x)
        {
            anim::Graph& g = x.g;
            LocomotionAnimation& a = x.a;
            Crouching& n = a.crouching_nodes;
            const LocomotionClips& k = x.k;

            refresh_rotate_in_place(x);

            Cache directions[6];
            auto direction = [&](int i) {
                return cached(g, directions[i], [&] {
                    return g.play(n.walk[i], k.crouch_walk[i], { .rate = a.crouching.play_rate, .sync = "Movement" });
                });
            };

            Cache movement;
            auto movement_pose = [&] {
                return cached(g, movement, [&] {
                    refresh_grounded_movement(x);
                    refresh_crouching_movement(x);
                    enter_on_relevance(g, n.movement_relevance, n.movement, Move::forward);
                    const anim::Transition direction_change = crossfade(0.7f, anim::BlendCurve::cubic, &a.move_direction_change_profile);
                    update_movement_states(a, n.movement, direction_change, direction_change, direction_change);
                    anim::PoseRef pose = g.blend(n.stride_alpha.update(a.crouching.stride_blend, g.dt),
                        [&] { return g.evaluate(k.crouch_walk_pose, 0.0f); },
                        [&] { return g.state_machine(n.movement, [&](Move m) { return movement_state_pose(x, m, direction); }); });
                    pose = g.additive(std::move(pose), AdditiveKind::local, 1.0f,
                        [&] { return g.evaluate(n.lean, *k.lean, glm::vec2(a.lean_right, a.lean_forward), 0.0f); });
                    set_curve(pose, curve::PoseMoving, 1.0f);
                    return pose;
                });
            };

            using State = Crouching::State;
            enter_on_relevance(g, n.states_relevance, n.states, State::idle);
            const bool moving = a.locomotion.moving_smooth;
            const State before = n.states.state();
            switch (n.states.state())
            {
            case State::idle:
                if (moving)
                    n.states.transition(State::move, crossfade(0.2f));
                else if (a.rotating_left)
                    n.states.transition(State::rotate_left, inertial(0.2f));
                else if (a.rotating_right)
                    n.states.transition(State::rotate_right, inertial(0.2f));
                break;
            case State::move:
                if (!moving && n.states.current_weight() >= 1.0f)
                    n.states.transition(State::stop, crossfade(0.1f));
                else if (!moving)
                    n.states.transition(State::idle, crossfade(0.2f));
                break;
            case State::stop:
                if (n.states.current_weight() >= 1.0f)
                    n.states.transition(State::idle, crossfade(0.5f, anim::BlendCurve::linear, &a.quick_feet_profile));
                break;
            case State::rotate_left:
                if (finished(n.rotate_left) && !a.rotating_left)
                    n.states.transition(State::idle, crossfade(0.0f));
                else if (moving)
                    n.states.transition(State::idle, crossfade(0.4f));
                else if (a.rotating_right)
                    n.states.transition(State::rotate_right, inertial(0.2f));
                break;
            case State::rotate_right:
                if (finished(n.rotate_right) && !a.rotating_right)
                    n.states.transition(State::idle, crossfade(0.0f));
                else if (moving)
                    n.states.transition(State::idle, crossfade(0.4f));
                else if (a.rotating_left)
                    n.states.transition(State::rotate_left, inertial(0.2f));
                break;
            }
            if (n.states.state() != before)
            {
                if (before == State::idle || n.states.in(State::move))
                    stop_transition_and_turn_in_place(x);
                if (n.states.in(State::stop))
                    play_transition_right(x, 0.2f, 0.2f, 1.5f, 0.2f);
            }

            anim::PoseRef pose = g.inertialize(n.states_inertialization, [&] {
                return g.state_machine(n.states, [&](State s) -> anim::PoseRef {
                    switch (s)
                    {
                    case State::idle:
                    {
                        if (g.becoming_relevant(n.idle_relevance))
                            a.turn_in_place_activation_delay = 0.0f;
                        refresh_turn_in_place(x);
                        refresh_dynamic_transitions(x);
                        anim::PoseRef idle = g.evaluate(k.crouch_pose, 0.0f);
                        set_curve(idle, curve::FootLeftLock, 1.0f);
                        set_curve(idle, curve::FootRightLock, 1.0f);
                        set_curve(idle, curve::AllowTransitions, 1.0f);
                        idle = g.slot(turn_in_place_crouching_slot, std::move(idle));
                        scale_curve(idle, curve::RotationYawSpeed, a.turn_in_place_play_rate);
                        return idle;
                    }
                    case State::move: return movement_pose();
                    case State::stop:
                    {
                        // both legs to the crouch pose, feet locked
                        anim::PoseRef pose = movement_pose();
                        pose = g.layered(std::move(pose), a.leg_left_mask, 1.0f, [&] { return g.evaluate(k.crouch_walk_pose, 0.0f); }, true);
                        pose = g.layered(std::move(pose), a.leg_right_mask, 1.0f, [&] { return g.evaluate(k.crouch_walk_pose, 0.0f); }, true);
                        set_curve(pose, curve::FootLeftLock, 1.0f);
                        set_curve(pose, curve::FootRightLock, 1.0f);
                        return pose;
                    }
                    case State::rotate_left:
                    case State::rotate_right:
                    {
                        const bool left = s == State::rotate_left;
                        anim::PoseRef rotate = g.play(left ? n.rotate_left : n.rotate_right, left ? k.crouch_rotate_left : k.crouch_rotate_right,
                            { .rate = a.rotate_in_place_play_rate, .loop = left ? a.rotating_left : a.rotating_right });
                        scale_curve(rotate, curve::RotationYawSpeed, a.rotate_in_place_play_rate);
                        return rotate;
                    }
                    }
                    return g.bind_pose();
                });
            });
            set_curve(pose, curve::PoseCrouching, 1.0f);
            return pose;
        }
    }


    // ---------------------------------------------------------------- AB_Als_Grounded

    anim::PoseRef grounded_pose(Context& x)
    {
        anim::Graph& g = x.g;
        LocomotionAnimation& a = x.a;
        Grounded& n = a.grounded_nodes;
        using State = Grounded::State;

        const bool active = a.locomotion.moving_smooth || a.rotating_left || a.rotating_right;
        if (g.becoming_relevant(n.relevance))
        {
            // InitializeGrounded; the entry conduit picks the stance of the last pose, or the end of a roll
            a.grounded.velocity_blend_initialization_required = true;
            const State entry = a.grounded_entry == GroundedEntryMode::from_roll && !active ? State::roll
                : a.pose.standing >= 0.5f ? State::standing : a.pose.crouching >= 0.5f ? State::crouching : State::standing;
            n.states.current = (int32_t)entry;
            n.states.fading.clear();
            n.states.reset_current = true;
            n.states.state_time = 0.0f;
            n.states.entered_frame = g.frame;
        }
        refresh_grounded(x);

        const bool standing = a.stance == Stance::standing;
        const bool crouching = a.stance == Stance::crouching;
        const anim::Transition stance_change = crossfade(0.5f, &a.stance_change_curve);   // CF_Als_StanceChange
        const State before = n.states.state();
        switch (n.states.state())
        {
        // the aliases: Standing Alias = {crouching, standing to crouching, crouching to standing}, Crouching Alias the other way
        case State::standing:
            if (crouching && active)
                n.states.transition(State::crouching, stance_change);
            else if (crouching)
                n.states.transition(State::standing_to_crouching, inertial(0.3f));
            break;
        case State::crouching:
            if (standing && active)
                n.states.transition(State::standing, stance_change);
            else if (standing)
                n.states.transition(State::crouching_to_standing, inertial(0.3f));
            break;
        case State::standing_to_crouching:
            if (standing && active)
                n.states.transition(State::standing, stance_change);
            else if (standing)
                n.states.transition(State::crouching_to_standing, inertial(0.3f));
            else if (finished(n.stand_to_crouch))
                n.states.transition(State::crouching, crossfade(0.0f));
            break;
        case State::crouching_to_standing:
            if (crouching && active)
                n.states.transition(State::crouching, stance_change);
            else if (crouching)
                n.states.transition(State::standing_to_crouching, inertial(0.3f));
            else if (finished(n.crouch_to_stand))
                n.states.transition(State::standing, crossfade(0.0f));
            break;
        case State::roll:
            if (standing)
                n.states.transition(State::standing, crossfade(0.6f, anim::BlendCurve::cubic));
            else if (crouching)
                n.states.transition(State::crouching, crossfade(0.6f, anim::BlendCurve::cubic));
            break;
        }
        if (n.states.state() != before)
        {
            // state entry / exit functions
            if (n.states.in(State::standing_to_crouching) || n.states.in(State::crouching_to_standing))
                stop_transition_and_turn_in_place(x);
            if (before == State::roll)
                play_transition_right(x, 0.2f, 0.2f, 1.5f, 0.2f);   // PlayRollToGroundedTransitionAnimation
        }
        // Standing, Crouching and Roll reset the entry mode when they become relevant
        if (n.states.just_entered(g.frame) && (n.states.in(State::standing) || n.states.in(State::crouching) || n.states.in(State::roll)))
            a.grounded_entry = GroundedEntryMode::none;

        anim::PoseRef pose = g.inertialize(n.inertialization, [&] {
            return g.state_machine(n.states, [&](State s) -> anim::PoseRef {
                switch (s)
                {
                case State::standing: return standing_pose(x);
                case State::crouching: return crouching_pose(x);
                case State::standing_to_crouching: return g.play(n.stand_to_crouch, x.k.stand_to_crouch, { .rate = 1.2f, .loop = false });
                case State::crouching_to_standing: return g.play(n.crouch_to_stand, x.k.crouch_to_stand, { .rate = 1.2f, .loop = false });
                case State::roll:
                {
                    // the end of a roll, the feet where they landed
                    anim::PoseRef roll = g.evaluate_frame(x.k.roll, 45);
                    set_curve(roll, curve::FootLeftLock, 1.0f);
                    set_curve(roll, curve::FootRightLock, 1.0f);
                    return roll;
                }
                }
                return g.bind_pose();
            });
        });
        set_curve(pose, curve::PoseGrounded, 1.0f);
        set_curve(pose, curve::FootLeftIk, 1.0f);
        set_curve(pose, curve::FootRightIk, 1.0f);
        return pose;
    }

    namespace
    {
        // ---------------------------------------------------------------- debug

        const char* name(Standing::State s)
        {
            switch (s)
            {
            case Standing::State::idle: return "idle";
            case Standing::State::move: return "move";
            case Standing::State::stop: return "stop";
            case Standing::State::rotate_left: return "rotate left";
            case Standing::State::rotate_right: return "rotate right";
            }
            return "?";
        }
        const char* name(Standing::Detail d)
        {
            switch (d)
            {
            case Standing::Detail::walk: return "walk";
            case Standing::Detail::run: return "run";
            case Standing::Detail::run_start: return "run start";
            case Standing::Detail::run_start_from_walk: return "run start from walk";
            case Standing::Detail::first_pivot: return "first pivot";
            case Standing::Detail::second_pivot: return "second pivot";
            }
            return "?";
        }
        const char* name(Move m)
        {
            switch (m)
            {
            case Move::forward: return "F";
            case Move::backward: return "B";
            case Move::right_forward: return "RF";
            case Move::right_backward: return "RB";
            case Move::left_forward: return "LF";
            case Move::left_backward: return "LB";
            }
            return "?";
        }
        const char* name(Grounded::State s)
        {
            switch (s)
            {
            case Grounded::State::standing: return "standing";
            case Grounded::State::crouching: return "crouching";
            case Grounded::State::standing_to_crouching: return "standing -> crouching";
            case Grounded::State::crouching_to_standing: return "crouching -> standing";
            case Grounded::State::roll: return "roll";
            }
            return "?";
        }
        const char* name(LocomotionAnimation::InAir::State s)
        {
            using State = LocomotionAnimation::InAir::State;
            switch (s)
            {
            case State::grounded: return "grounded";
            case State::fall: return "fall";
            case State::jump: return "jump";
            case State::land: return "land";
            case State::land_movement: return "land movement";
            }
            return "?";
        }

        void debug_lines(anim::Animator& animator, const LocomotionAnimation& a, const LocomotionCharacter& c)
        {
            char buffer[160];
            auto line = [&](const char* label, const char* format, auto... values) {
                std::snprintf(buffer, sizeof(buffer), format, values...);
                animator.debug_value(label, buffer);
            };
            line("locomotion", "%s, action %s", to_string(c.locomotion_mode), to_string(c.action));
            line("gait", "%s (desired %s, max %s), amount %.2f", to_string(c.gait), to_string(c.desired_gait), to_string(c.max_allowed_gait), c.gait_amount);
            line("stance", "%s (desired %s)", to_string(c.stance), to_string(c.desired_stance));
            line("rotation mode", "%s (desired %s)", to_string(c.rotation_mode), to_string(c.desired_rotation_mode));
            line("speed", "%.2f m/s (%.0f cm/s ALS), max %.2f, moving %d smooth %d", c.locomotion.speed, a.locomotion.speed_cm, c.max_speed,
                (int)a.locomotion.moving, (int)a.locomotion.moving_smooth);
            line("yaw", "actor %.1f, target %.1f, velocity %.1f, view %.1f (%.1f)", c.yaw, c.locomotion.target_yaw, c.locomotion.velocity_yaw,
                c.view.yaw, a.view.yaw_angle);
            line("locomotion states", "%s, jump %d, vertical %.0f cm/s, ground prediction %.2f", name(a.in_air_nodes.states.state()),
                (int)a.in_air_nodes.jump.state(), a.in_air.vertical_velocity, a.in_air.ground_prediction);
            line("grounded", "%s%s", name(a.grounded_nodes.states.state()), a.grounded_entry == GroundedEntryMode::from_roll ? " (entry from roll)" : "");
            line("standing", "%s, details %s, movement %s", name(a.standing_nodes.states.state()), name(a.standing_nodes.details.state()),
                name(a.standing_nodes.movement.state()));
            line("velocity blend", "F %.2f B %.2f L %.2f R %.2f", a.grounded.forward, a.grounded.backward, a.grounded.left, a.grounded.right);
            line("stride / walk-run / rate", "%.2f / %.0f / %.2f", a.standing.stride_blend, a.standing.walk_run_blend, a.standing.play_rate);
            line("lean", "right %.2f forward %.2f", a.lean_right, a.lean_forward);
            line("pose", "grounded %.2f in air %.2f standing %.2f crouching %.2f gait %.2f", a.pose.grounded, a.pose.in_air, a.pose.standing,
                a.pose.crouching, a.pose.gait);
            line("rotate / turn in place", "%s%s rate %.2f, turn delay %.2f", a.rotating_left ? "left " : "", a.rotating_right ? "right " : "",
                a.rotate_in_place_play_rate, a.turn_in_place_activation_delay);
            const int32_t overlay_count = a.clips ? (int32_t)a.clips->overlays.size() : 0;
            line("overlay", "%s", a.overlay >= 0 && a.overlay < overlay_count ? a.clips->overlays[a.overlay].name.c_str() : "-");
            line("layering", "head %.2f spine %.2f pelvis %.2f legs %.2f arms %.2f / %.2f", a.layering.head, a.layering.spine, a.layering.pelvis,
                a.layering.legs, a.layering.arm_left, a.layering.arm_right);
            line("head / spine", "head pitch %.1f yaw %.1f blend %.2f, spine yaw %.1f", a.head.pitch_angle, a.head.yaw_angle, a.view.head_blend,
                a.spine.final_yaw_angle);
            line("feet", "lock L %.2f R %.2f, offset L %.3f R %.3f, pelvis %.3f m", a.feet.left.lock_amount, a.feet.right.lock_amount,
                a.feet.left.offset_z, a.feet.right.offset_z, a.feet.pelvis_offset);
        }

        [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<anim::Evaluate>, =ecs::in_set<LocomotionPresentation>]]
        void animate_locomotion_characters(ecs::Query<anim::Animator, LocomotionAnimation, const LocomotionCharacter> characters,
            ecs::Query<anim::PoseLibrary> pose_libraries, ecs::Res<phys::PhysicsScene> physics, ecs::Res<ecs::FrameTime> time)
        {
            const float dt = std::min((float)time->dt, 0.1f);   // hitches (shader compilation, loading)
            characters.each([&](ecs::Entity e, anim::Animator& animator, LocomotionAnimation& a, const LocomotionCharacter& c) {
                if (!animator.valid() || !a.clips || !c.settings)
                    return;
                anim::PoseLibrary* poses = pose_libraries.get(e);
                // moving leaves a pose
                if (poses && poses->active >= 0 && c.locomotion.has_input)
                    poses->select(-1);

                refresh_from_character(a, c, c.settings->animation, dt, (float)time->alpha);
                refresh_thread_safe(a, animator);
                refresh_layering(a, animator);

                anim::Graph g = animator.begin_update(dt);
                Context x{ g, a, c, c.settings->animation, *a.clips, animator, *physics, physics->get_character_body(c.capsule) };
                refresh_view(x);

                // AB_Als: locomotion, the PostLocomotion slot (actions), layering with the overlay, head, control rig
                anim::PoseRef pose = g.slot(post_locomotion_slot, [&] { return locomotion_pose(x); });
                pose = layering_pose(x, std::move(pose));
                pose = head_pose(x, std::move(pose));
                if (poses)
                    pose = anim::apply_pose_library(g, *poses, std::move(pose));
                if (a.control_rig)
                {
                    const glm::mat4 model_to_world = glm::translate(glm::mat4(1.0f), a.locomotion.position)
                        * glm::mat4_cast(yaw_to_rotation(a.locomotion.yaw)) * animator.rig->skeleton->root_transform;
                    apply_control_rig(x, *pose, model_to_world);
                }
                animator.end_update(g, std::move(pose));
                a.pending_update = false;

                // notifies of this update
                if (animator.has_event("AlsSetGroundedEntryMode"))
                    a.grounded_entry = GroundedEntryMode::from_roll;
                debug_lines(animator, a, c);

                if (debug_log_enabled())
                {
                    char state[200];
                    std::snprintf(state, sizeof(state), "%s | %s | %s | %s | %s | action %s", name(a.in_air_nodes.states.state()),
                        name(a.grounded_nodes.states.state()), name(a.standing_nodes.states.state()), name(a.standing_nodes.details.state()),
                        name(a.standing_nodes.movement.state()), to_string(a.action));
                    static std::string last;
                    if (last != state)
                    {
                        last = state;
                        LogLocomotionAnimation.Log("Animation: %s", state);
                    }
                }
            });
        }

        ECS_REGISTER()
        SCENE_REGISTER_COMPONENTS(LocomotionAnimation)
    }

    bool init_locomotion_animation(World& world, ecs::Entity e)
    {
        ecs::Registry& registry = world.registry;
        const SkinnedMesh* skinned = registry.get<SkinnedMesh>(e);
        const LocomotionCharacter* character = registry.get<LocomotionCharacter>(e);
        if (!skinned || !skinned->pose || !character || !character->settings)
        {
            LogLocomotionAnimation.Log("Entity '%s' has no skinned mesh or locomotion character", scene::get_name(registry, e).c_str());
            return false;
        }

        static std::shared_ptr<const LocomotionClips> shared_clips;
        if (!shared_clips)
        {
            auto clips = std::make_shared<LocomotionClips>();
            if (!clips->load())
            {
                LogLocomotionAnimation.Log("ALS animation set incomplete (assets/animations/als, tools/ue_import/import_als.py)");
                return false;
            }
            shared_clips = clips;
        }

        anim::Animator animator;
        std::shared_ptr<anim::Rig> rig = anim::Rig::create(skinned->get_skeleton(), curve_names);
        animator.init(rig);

        LocomotionAnimation a;
        a.clips = shared_clips;
        a.locomotion.scale = character->size_scale;
        a.leg_left_mask = rig->make_mask({ { "thigh_l" }, { "ik_foot_l" } });
        a.leg_right_mask = rig->make_mask({ { "thigh_r" }, { "ik_foot_r" } });
        auto profiles = anim::load_blend_profiles("animations/als/blend_profiles.json", *rig);
        if (auto it = profiles.find("MoveDirectionChange"); it != profiles.end())
            a.move_direction_change_profile = it->second;
        if (auto it = profiles.find("QuickFeetBlend"); it != profiles.end())
            a.quick_feet_profile = it->second;
        if (const SampledCurve* stance_change = character->settings->animation.stance_change_curve)
            for (int i = 0; i <= 32; ++i)
                a.stance_change_curve.samples.push_back(stance_change->evaluate_x(float(i) / 32.0f));

        // the capsule of the ground prediction sweeps: standing, the size of the character
        const float radius = character->scaled(character->settings->capsule_radius);
        const float height = character->scaled(character->settings->standing_height);
        a.ground_prediction_shape = world.get_physics().create_shape(phys::CapsuleShape{ .half_height = std::max(height * 0.5f - radius, 0.01f), .radius = radius });

        // UE node settings (AB_Als_Standing / Crouching / Locomotion)
        auto& sn = a.standing_nodes;
        sn.sprint_acceleration_alpha = { .map_range = true, .in_min = 0.0f, .in_max = 0.25f, .interpolate = true,
            .speed_increasing = 20.0f, .speed_decreasing = 4.0f };
        sn.lean_alpha = { .clamp = true, .clamp_min = 0.5f, .clamp_max = 1.0f, .interpolate = true,
            .speed_increasing = 20.0f, .speed_decreasing = 1.0f };
        a.crouching_nodes.stride_alpha = { .interpolate = true };
        // turning curves switch at once, never inertialized
        sn.states_inertialization.filtered_curves = { curve::RotationYawSpeed };
        a.crouching_nodes.states_inertialization.filtered_curves = { curve::RotationYawSpeed };

        auto& in = a.in_air_nodes;
        in.inertialization.filtered_curves = { curve::RotationYawSpeed };
        in.fall_fast_alpha = { .map_range = true, .in_min = 0.0f, .in_max = -1000.0f, .interpolate = true, .speed_increasing = 5.0f, .speed_decreasing = 5.0f };
        in.flail_alpha = { .map_range = true, .in_min = -500.0f, .in_max = -3000.0f, .interpolate = true, .speed_increasing = 5.0f, .speed_decreasing = 5.0f };
        in.fall_ground_prediction_alpha = { .interpolate = true, .speed_increasing = 20.0f, .speed_decreasing = 5.0f };
        in.jump_ground_prediction_alpha = in.fall_ground_prediction_alpha;
        in.jump_left_alpha = { .map_range = true, .in_min = 200.0f, .in_max = 500.0f, .interpolate = true, .speed_increasing = 5.0f, .speed_decreasing = 5.0f };
        in.jump_right_alpha = in.jump_left_alpha;
        in.land_aiming_alpha = { .map_range = true, .in_min = 0.0f, .in_max = 1.0f, .out_min = 1.0f, .out_max = 0.5f, .interpolate = true,
            .speed_increasing = 2.5f, .speed_decreasing = 20.0f };

        init_layering(a, *rig);
        init_feet(a, *rig);

        registry.add<anim::Animator>(e, std::move(animator));
        registry.add<LocomotionAnimation>(e, std::move(a));
        return true;
    }

    int32_t find_overlay(const LocomotionAnimation& a, std::string_view name)
    {
        if (!a.clips)
            return -1;
        for (size_t i = 0; i < a.clips->overlays.size(); ++i)
        {
            const std::string& n = a.clips->overlays[i].name;
            if (n.size() == name.size() && std::equal(n.begin(), n.end(), name.begin(), [](char x, char y) { return std::tolower(x) == std::tolower(y); }))
                return (int32_t)i;
        }
        return -1;
    }
}
