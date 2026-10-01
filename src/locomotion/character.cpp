module;

#include <json/value.h>

module locomotion;

import std.compat;
import glm;
import rhmath;
import ecs;
import framework;
import physics;
import input;
import assets;
import rhcomponents;
import animation;
import sky_controller;
import cvar;
import log;
import fixed_string;

#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogLocomotion, Log);

namespace loco
{
    namespace
    {
        constexpr float camera_distance = 3.2f;
        constexpr float camera_pivot_height = 1.45f;
        constexpr float camera_probe_radius = 0.2f;

        constexpr float sprint_hold_time = 0.1f;   // ALS IA_Als_Sprint hold trigger
        constexpr float roll_tap_time = 0.3f;      // ALS IA_Als_Roll tap trigger
        constexpr float landing_friction_time = 0.5f;

        std::optional<LocomotionInput> scripted_input;

        cvar::Var<bool> cv_log("loco.debug.log", false, "Logs state changes of locomotion characters (gait, stance, modes, landing)");
        cvar::Var<bool> cv_velocity_direction("loco.player.velocity_direction", false,
            "The player character turns towards where it moves, never towards the camera (ALS velocity direction rotation "
            "mode); off: it faces the view direction. Key 1 toggles it");

        struct LocomotionCameraProbe
        {
            phys::Shape shape;
        };

        // `player.teleport <x> <y> <z>` (console, Terrain window): the player's feet, applied at the next fixed tick
        std::optional<glm::vec3> pending_teleport;

        cvar::Command teleport_command("player.teleport", "Moves the player character (feet) to a world position",
            [] (cvar::Args args) {
                auto parse = [] (const std::string& text, float& value) {
                    return std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{};
                };
                glm::vec3 position;
                if (args.size() != 3 || !parse(args[0], position.x) || !parse(args[1], position.y) || !parse(args[2], position.z))
                {
                    cvar::print("player.teleport <x> <y> <z>", cvar::Output::error);
                    return;
                }
                pending_teleport = position;
            }, "<x> <y> <z>");

        // `player.input <x> <y> [walk] [sprint] [jump] [crouch] [aim] [roll]` / `player.input off`
        cvar::Command input_command("player.input",
            "Drives the player character: move axis x (right) y (forward), held flags sprint / jump / aim, one-shot "
            "flags walk / crouch / roll / rotation (toggles); 'off' returns the keyboard",
            [] (cvar::Args args) {
                if (args.size() == 1 && args[0] == "off")
                {
                    scripted_input.reset();
                    return;
                }
                LocomotionInput command;
                if (args.size() < 2
                    || std::from_chars(args[0].data(), args[0].data() + args[0].size(), command.move_axis.x).ec != std::errc{}
                    || std::from_chars(args[1].data(), args[1].data() + args[1].size(), command.move_axis.y).ec != std::errc{})
                {
                    cvar::print("player.input <x> <y> [sprint] [jump] [aim] [walk] [crouch] [roll] [rotation] | player.input off", cvar::Output::error);
                    return;
                }
                for (size_t i = 2; i < args.size(); ++i)
                {
                    command.sprint |= args[i] == "sprint";
                    command.jump |= args[i] == "jump";
                    command.aim |= args[i] == "aim";
                    command.toggle_walk |= args[i] == "walk";
                    command.toggle_crouch |= args[i] == "crouch";
                    command.roll |= args[i] == "roll";
                    command.cycle_rotation_mode |= args[i] == "rotation";
                }
                scripted_input = command;
            }, "<x> <y> [sprint] [jump] [aim] [walk] [crouch] [roll] [rotation] | off");

        // camera yaw (radians, Rhea camera looks -Z at 0) -> ALS view yaw of its forward direction
        float camera_to_view_yaw(float camera_yaw)
        {
            return unwind_degrees(180.0f - camera_yaw * (180.0f / pi));
        }

        glm::vec3 camera_forward(float yaw, float pitch)
        {
            return glm::vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
        }

        float animation_curve(const anim::Animator* animator, std::string_view name)
        {
            if (!animator || !animator->valid())
                return 0.0f;
            auto index = animator->rig->curves.find(name);
            return index ? animator->curve(*index) : 0.0f;
        }


        // ---------------------------------------------------------------- input

        [[=ecs::system<ecs::Phase::FixedPre>, =ecs::in_set<LocomotionSimulation>, =ecs::before<LocomotionInputSet>]]
        void teleport_locomotion_player(ecs::Query<LocomotionCharacter, const LocomotionPlayer> characters, ecs::ResMut<phys::PhysicsScene> physics)
        {
            if (!pending_teleport)
                return;
            characters.each([&](LocomotionCharacter& c, const LocomotionPlayer&) {
                c.position = c.previous_position = *pending_teleport;
                c.velocity = glm::vec3(0.0f);
                physics->set_character_position(c.capsule, *pending_teleport);
            });
            pending_teleport.reset();
        }

        void read_keyboard(LocomotionInput& command, LocomotionPlayer& player, const Input& input, float dt)
        {
            if (input.is_key_down(Key::W)) command.move_axis.y += 1.0f;
            if (input.is_key_down(Key::S)) command.move_axis.y -= 1.0f;
            if (input.is_key_down(Key::D)) command.move_axis.x += 1.0f;
            if (input.is_key_down(Key::A)) command.move_axis.x -= 1.0f;
            if (glm::length(command.move_axis) > 1.0f)
                command.move_axis = glm::normalize(command.move_axis);

            // Shift: held -> sprint, tapped -> roll
            if (input.is_key_down(Key::LeftShift))
            {
                player.shift_held = player.shift_held < 0.0f ? 0.0f : player.shift_held + dt;
                command.sprint = player.shift_held >= sprint_hold_time;
            }
            else
            {
                command.roll = player.shift_held >= 0.0f && player.shift_held < roll_tap_time;
                player.shift_held = -1.0f;
            }

            command.jump = input.is_key_down(Key::Space);
            command.aim = input.is_key_down(Key::MouseRight);

            auto pressed = [&](Key key, bool& was_down) {
                const bool down = input.is_key_down(key);
                const bool edge = down && !was_down;
                was_down = down;
                return edge;
            };
            command.toggle_walk = pressed(Key::LeftAlt, player.alt_was_down);
            command.toggle_crouch = pressed(Key::LeftControl, player.ctrl_was_down);
            command.cycle_rotation_mode = pressed(Key::_1, player.one_was_down);
        }

        [[=ecs::system<ecs::Phase::FixedPre>, =ecs::in_set<LocomotionInputSet>]]
        void read_locomotion_player_input(ecs::Query<LocomotionInput, LocomotionPlayer> players, ecs::Res<Input> input,
            ecs::Res<ecs::SimTime> time)
        {
            const float dt = (float)time->dt;
            players.each([&](LocomotionInput& command, LocomotionPlayer& player) {
                // a disabled player (free camera) keeps the last view: the character does not follow the camera
                const float view_yaw = command.view_yaw, view_pitch = command.view_pitch;
                command = {};
                if (!player.enabled)
                {
                    command.view_yaw = view_yaw;
                    command.view_pitch = view_pitch;
                    return;
                }
                command.view_yaw = camera_to_view_yaw(player.camera_yaw);
                command.view_pitch = player.camera_pitch * (180.0f / pi);

                if (scripted_input)
                {
                    const float yaw = command.view_yaw, pitch = command.view_pitch;
                    command = *scripted_input;
                    command.view_yaw = yaw;
                    command.view_pitch = pitch;
                    // one-shot flags fire once
                    scripted_input->toggle_walk = scripted_input->toggle_crouch = false;
                    scripted_input->roll = scripted_input->cycle_rotation_mode = false;
                }
                else
                    read_keyboard(command, player, *input, dt);

                // the rotation mode of the player is a setting: the key toggles it
                if (command.cycle_rotation_mode)
                    cv_velocity_direction.set(!cv_velocity_direction.get());
                command.cycle_rotation_mode = false;
                command.set_rotation_mode = true;
                command.rotation_mode = cv_velocity_direction.get() ? RotationMode::velocity_direction : RotationMode::view_direction;
            });
        }




        // ---------------------------------------------------------------- AAlsCharacter port

        void set_target_yaw(LocomotionCharacter& c, float target)
        {
            c.locomotion.target_yaw = unwind_degrees(target);
            c.locomotion.smooth_target_yaw = c.locomotion.target_yaw;
            c.locomotion.target_yaw_view_space = unwind_degrees(c.view.yaw - c.locomotion.target_yaw);
        }

        void set_target_yaw_smooth(LocomotionCharacter& c, float target, float dt, float speed)
        {
            c.locomotion.target_yaw = unwind_degrees(target);
            c.locomotion.smooth_target_yaw = interpolate_angle_constant(c.locomotion.smooth_target_yaw, c.locomotion.target_yaw, dt, speed);
            c.locomotion.target_yaw_view_space = unwind_degrees(c.view.yaw - c.locomotion.target_yaw);
        }

        void refresh_target_yaw_using_actor_rotation(LocomotionCharacter& c)
        {
            set_target_yaw(c, c.yaw);
        }

        void set_rotation_smooth(LocomotionCharacter& c, float target, float dt, float half_life)
        {
            set_target_yaw(c, target);
            c.yaw = damper_exact_angle(c.yaw, c.locomotion.smooth_target_yaw, dt, half_life);
        }

        void set_rotation_extra_smooth(LocomotionCharacter& c, float target, float dt, float half_life, float target_speed)
        {
            set_target_yaw_smooth(c, target, dt, target_speed);
            c.yaw = damper_exact_angle(c.yaw, c.locomotion.smooth_target_yaw, dt, half_life);
        }

        void apply_desired_stance(LocomotionCharacter& c, phys::PhysicsScene& physics)
        {
            Stance want = c.stance;
            if (c.action == LocomotionAction::none)
            {
                if (c.locomotion_mode == LocomotionMode::grounded)
                    want = c.desired_stance;
                else if (c.locomotion_mode == LocomotionMode::in_air)
                    want = Stance::standing;
            }
            else if (c.action == LocomotionAction::rolling)
                want = Stance::crouching;

            if (want == c.stance)
                return;
            const float height = c.scaled(want == Stance::crouching ? c.settings->crouched_height : c.settings->standing_height);
            // standing up under a low ceiling fails: tried again next tick
            if (physics.set_character_height(c.capsule, height))
            {
                c.capsule_height = height;
                c.stance = want;
            }
        }

        void refresh_gait_settings(LocomotionCharacter& c)
        {
            GaitSettings g = c.settings->gait(c.rotation_mode, c.stance);
            g.walk_forward_speed = c.scaled(g.walk_forward_speed);
            g.walk_backward_speed = c.scaled(g.walk_backward_speed);
            g.run_forward_speed = c.scaled(g.run_forward_speed);
            g.run_backward_speed = c.scaled(g.run_backward_speed);
            g.sprint_speed = c.scaled(g.sprint_speed);
            c.gait_settings = g;
        }

        bool can_sprint(const LocomotionCharacter& c)
        {
            const LocomotionSettings& s = *c.settings;
            if (!c.locomotion.has_input || c.stance != Stance::standing ||
                ((c.desired_aiming || c.desired_rotation_mode == RotationMode::aiming) && !s.sprint_has_priority_over_aiming))
                return false;
            if (c.view_mode != ViewMode::first_person &&
                (c.desired_rotation_mode == RotationMode::velocity_direction || s.rotate_to_velocity_when_sprinting))
                return true;
            return std::abs(unwind_degrees(c.locomotion.input_yaw - c.view.yaw)) < 50.0f;
        }

        // UAlsCharacterMovementComponent::RefreshGroundedMovementSettings
        void refresh_grounded_movement_settings(LocomotionCharacter& c)
        {
            const GaitSettings& g = c.gait_settings;
            float walk = g.walk_forward_speed;
            float run = g.run_forward_speed;
            if (g.direction_dependent_speed && glm::dot(c.velocity, c.velocity) > 1e-8f)
            {
                const float velocity_yaw_view_space = unwind_degrees(direction_to_yaw(c.velocity) - c.view.yaw);
                const glm::vec2 range = c.settings->velocity_angle_to_speed_range;
                const float forward = 1.0f - clamp01((std::abs(velocity_yaw_view_space) - range.x) / (range.y - range.x));
                walk = glm::mix(g.walk_backward_speed, g.walk_forward_speed, forward);
                run = glm::mix(g.run_backward_speed, g.run_forward_speed, forward);
            }

            const float speed = glm::length(glm::vec2(c.velocity.x, c.velocity.z));
            if (speed > run)
                c.gait_amount = map_range_clamped(speed, run, g.sprint_speed, 2.0f, 3.0f);
            else if (speed > walk)
                c.gait_amount = map_range_clamped(speed, walk, run, 1.0f, 2.0f);
            else
                c.gait_amount = map_range_clamped(speed, 0.0f, walk, 0.0f, 1.0f);

            switch (c.max_allowed_gait)
            {
            case Gait::walking: c.max_speed = walk; break;
            case Gait::running: c.max_speed = run; break;
            case Gait::sprinting: c.max_speed = g.sprint_speed; break;
            }

            if (g.acceleration_deceleration_friction)
            {
                const glm::vec3 v = g.acceleration_deceleration_friction->evaluate(c.gait_amount);
                c.max_acceleration = c.scaled(v.x);
                c.braking_deceleration = c.scaled(v.y);
                c.ground_friction = v.z;
            }
        }

        void refresh_gait(LocomotionCharacter& c)
        {
            if (c.locomotion_mode != LocomotionMode::grounded)
                return;

            // max allowed gait
            Gait max_allowed = c.desired_gait;
            if (c.desired_gait == Gait::sprinting && !can_sprint(c))
                max_allowed = Gait::running;
            c.max_allowed_gait = max_allowed;
            refresh_grounded_movement_settings(c);

            // actual gait: from the real speed
            const GaitSettings& g = c.gait_settings;
            const float margin = c.scaled(0.1f);
            if (c.locomotion.speed < g.max_walk_speed() + margin)
                c.gait = Gait::walking;
            else if (c.locomotion.speed < g.max_run_speed() + margin || max_allowed != Gait::sprinting)
                c.gait = Gait::running;
            else
                c.gait = Gait::sprinting;
        }

        void set_rotation_mode(LocomotionCharacter& c, RotationMode mode)
        {
            if (c.rotation_mode == mode)
                return;
            c.rotation_mode = mode;
            refresh_gait_settings(c);
            // no rotating to the last input direction after the mode changed while standing still
            c.locomotion.rotation_towards_last_input_direction_blocked = true;
        }

        void refresh_rotation_mode(LocomotionCharacter& c)
        {
            const LocomotionSettings& s = *c.settings;
            const bool aiming = c.desired_aiming || c.desired_rotation_mode == RotationMode::aiming;
            const bool sprinting = c.max_allowed_gait == Gait::sprinting;

            if (c.view_mode == ViewMode::first_person)
            {
                if (c.locomotion_mode == LocomotionMode::in_air)
                    set_rotation_mode(c, aiming && s.allow_aiming_when_in_air ? RotationMode::aiming : RotationMode::view_direction);
                else
                    set_rotation_mode(c, aiming && (!sprinting || !s.sprint_has_priority_over_aiming) ? RotationMode::aiming : RotationMode::view_direction);
                return;
            }

            if (c.locomotion_mode == LocomotionMode::in_air)
            {
                if (aiming && s.allow_aiming_when_in_air)
                    set_rotation_mode(c, RotationMode::aiming);
                else if (aiming)
                    set_rotation_mode(c, RotationMode::view_direction);
                else
                    set_rotation_mode(c, c.desired_rotation_mode);
                return;
            }

            if (sprinting)
            {
                if (aiming && !s.sprint_has_priority_over_aiming)
                    set_rotation_mode(c, RotationMode::aiming);
                else if (s.rotate_to_velocity_when_sprinting)
                    set_rotation_mode(c, RotationMode::velocity_direction);
                else
                    set_rotation_mode(c, c.desired_rotation_mode);
            }
            else
                set_rotation_mode(c, aiming ? RotationMode::aiming : c.desired_rotation_mode);
        }

        float grounded_moving_rotation_half_life(const LocomotionCharacter& c)
        {
            const SampledCurve* curve = c.gait_settings.rotation_half_life;
            const float half_life = curve ? curve->evaluate_x(std::max(1.0f, c.gait_amount)) : 0.2f;
            return half_life * lerp_clamped(1.0f, 0.333333f, c.view.yaw_speed / 300.0f);
        }

        bool constrain_aiming_rotation(LocomotionCharacter& c, float& actor_yaw, float dt, bool secondary)
        {
            LocomotionState& l = c.locomotion;
            l.aiming_limit_applied_this_frame = true;
            if (l.reset_aiming_limit)
                l.aiming_yaw_angle_limit = 180.0f;

            float actor_yaw_view_space = unwind_degrees(c.view.yaw - actor_yaw);
            const float min_limit = c.settings->aiming_yaw_angle_limit;
            if (std::abs(actor_yaw_view_space) <= min_limit + 1e-4f)
            {
                l.aiming_yaw_angle_limit = min_limit;
                return false;
            }
            actor_yaw_view_space = remap_angle_for_counter_clockwise_rotation(actor_yaw_view_space);

            if (secondary)
            {
                const float target = std::clamp(actor_yaw_view_space, -min_limit, min_limit);
                const float delta = unwind_degrees(target - actor_yaw_view_space);
                if (std::abs(delta) < 1e-4f)
                    actor_yaw_view_space = target;
                else
                    actor_yaw_view_space = unwind_degrees(actor_yaw_view_space + delta * damper_exact_alpha(dt, 0.1f));
            }

            if (std::abs(actor_yaw_view_space) > l.aiming_yaw_angle_limit + 1e-4f)
                actor_yaw_view_space = std::clamp(actor_yaw_view_space, -l.aiming_yaw_angle_limit, l.aiming_yaw_angle_limit);
            else
                l.aiming_yaw_angle_limit = std::max(std::abs(actor_yaw_view_space), min_limit);

            const float previous = actor_yaw;
            actor_yaw = unwind_degrees(c.view.yaw - actor_yaw_view_space);
            return std::abs(unwind_degrees(actor_yaw - previous)) > 1e-4f;
        }

        void refresh_grounded_aiming_rotation(LocomotionCharacter& c, float dt)
        {
            float actor_yaw = c.yaw;
            if (!c.locomotion.has_input && !c.locomotion.moving)
            {
                set_target_yaw(c, c.view.yaw);
                if (!constrain_aiming_rotation(c, actor_yaw, dt, true))
                    return;
            }
            else
            {
                set_target_yaw_smooth(c, c.view.yaw, dt, 1000.0f);
                actor_yaw = damper_exact_angle(actor_yaw, c.locomotion.smooth_target_yaw, dt, 0.1f);
                if (constrain_aiming_rotation(c, actor_yaw, dt, false))
                    c.locomotion.smooth_target_yaw = c.locomotion.target_yaw;
            }
            c.yaw = actor_yaw;
        }

        void refresh_grounded_rotation(LocomotionCharacter& c, float dt, const anim::Animator* animator)
        {
            if (c.action != LocomotionAction::none || c.locomotion_mode != LocomotionMode::grounded)
                return;
            const LocomotionSettings& s = *c.settings;
            LocomotionState& l = c.locomotion;

            if (!l.moving)
            {
                // turn in place animations rotate the character by their RotationYawSpeed curve
                const float delta_yaw = animation_curve(animator, "RotationYawSpeed") * dt;
                if (std::abs(delta_yaw) > 1e-6f)
                {
                    c.yaw = unwind_degrees(c.yaw + delta_yaw);
                    refresh_target_yaw_using_actor_rotation(c);
                }

                if (c.rotation_mode == RotationMode::aiming || c.view_mode == ViewMode::first_person)
                {
                    refresh_grounded_aiming_rotation(c, dt);
                    return;
                }
                if (c.rotation_mode == RotationMode::velocity_direction)
                {
                    const float target = l.rotation_towards_last_input_direction_blocked ? l.target_yaw
                        : (s.rotate_towards_desired_velocity ? c.desired_velocity_yaw : l.velocity_yaw);
                    set_rotation_extra_smooth(c, target, dt, 0.1f, 800.0f);
                    return;
                }
                if (c.rotation_mode == RotationMode::view_direction)
                {
                    if ((!l.has_input && l.rotation_towards_last_input_direction_blocked) || !s.auto_rotate_on_any_input_while_not_moving)
                    {
                        refresh_target_yaw_using_actor_rotation(c);
                        return;
                    }
                    const float target = l.has_input ? c.view.yaw : l.target_yaw;
                    set_rotation_extra_smooth(c, target, dt, grounded_moving_rotation_half_life(c), 500.0f);
                    return;
                }
                refresh_target_yaw_using_actor_rotation(c);
                return;
            }

            // moving
            if (c.rotation_mode == RotationMode::aiming || c.view_mode == ViewMode::first_person)
            {
                refresh_grounded_aiming_rotation(c, dt);
                return;
            }
            if (c.rotation_mode == RotationMode::velocity_direction && (l.has_input || !l.rotation_towards_last_input_direction_blocked))
            {
                l.rotation_towards_last_input_direction_blocked = false;
                const float target = s.rotate_towards_desired_velocity ? c.desired_velocity_yaw : l.velocity_yaw;
                set_rotation_extra_smooth(c, target, dt, grounded_moving_rotation_half_life(c), 800.0f);
                return;
            }
            if (c.rotation_mode == RotationMode::view_direction && (l.has_input || !l.rotation_towards_last_input_direction_blocked))
            {
                l.rotation_towards_last_input_direction_blocked = false;
                const float target = c.gait == Gait::sprinting ? l.velocity_yaw
                    : c.view.yaw + animation_curve(animator, "RotationYawOffset");
                set_rotation_extra_smooth(c, target, dt, grounded_moving_rotation_half_life(c), 500.0f);
                return;
            }
            refresh_target_yaw_using_actor_rotation(c);
        }

        void refresh_in_air_rotation(LocomotionCharacter& c, float dt)
        {
            if (c.action != LocomotionAction::none || c.locomotion_mode != LocomotionMode::in_air)
                return;
            if (c.rotation_mode == RotationMode::aiming || c.view_mode == ViewMode::first_person)
            {
                set_target_yaw(c, c.view.yaw);
                float actor_yaw = damper_exact_angle(c.yaw, c.locomotion.smooth_target_yaw, dt, 0.1f);
                constrain_aiming_rotation(c, actor_yaw, dt, false);
                c.yaw = actor_yaw;
                return;
            }
            switch (c.settings->in_air_rotation_mode)
            {
            case InAirRotationMode::keep_view_space_rotation:
                set_rotation_smooth(c, c.view.yaw - c.locomotion.target_yaw_view_space, dt, 0.2f);
                return;
            case InAirRotationMode::rotate_to_velocity:
                if (c.locomotion.moving)
                {
                    set_rotation_smooth(c, c.locomotion.velocity_yaw, dt, 0.2f);
                    return;
                }
                break;
            case InAirRotationMode::keep_world_rotation:
                break;
            }
            refresh_target_yaw_using_actor_rotation(c);
        }

        // UCharacterMovementComponent::ApplyVelocityBraking
        void apply_velocity_braking(glm::vec3& v, float dt, float friction, float braking_deceleration, float friction_factor, float brake_to_stop)
        {
            friction = std::max(0.0f, friction * std::max(0.0f, friction_factor));
            braking_deceleration = std::max(0.0f, braking_deceleration);
            if (friction == 0.0f && braking_deceleration == 0.0f)
                return;
            const glm::vec3 old = v;
            const float length = glm::length(v);
            const glm::vec3 reverse = braking_deceleration > 0.0f && length > 0.0f ? -braking_deceleration * (v / length) : glm::vec3(0.0f);
            const float max_step = 1.0f / 33.0f;
            float remaining = dt;
            while (remaining >= 1e-6f)
            {
                const float step = remaining > max_step && friction > 0.0f ? std::min(max_step, remaining * 0.5f) : remaining;
                remaining -= step;
                v = v + (-friction * v + reverse) * step;
                if (glm::dot(v, old) <= 0.0f)
                {
                    v = glm::vec3(0.0f);
                    return;
                }
            }
            const float sq = glm::dot(v, v);
            if (sq <= 1e-8f || (braking_deceleration > 0.0f && sq <= brake_to_stop * brake_to_stop))
                v = glm::vec3(0.0f);
        }

        // UCharacterMovementComponent::CalcVelocity on the horizontal plane
        void calc_velocity(glm::vec3& v, const glm::vec3& acceleration, float dt, float friction, float braking_deceleration,
            float friction_factor, float max_speed, float brake_to_stop)
        {
            const bool zero_acceleration = glm::dot(acceleration, acceleration) < 1e-10f;
            const float speed = glm::length(v);
            const bool over_max = speed > max_speed * 1.01f;
            if (zero_acceleration || over_max)
            {
                const glm::vec3 old = v;
                apply_velocity_braking(v, dt, friction, braking_deceleration, friction_factor, brake_to_stop);
                if (over_max && glm::length(v) < max_speed && glm::dot(acceleration, old) > 0.0f)
                    v = glm::normalize(old) * max_speed;
            }
            else
            {
                // friction affects the ability to change direction
                const glm::vec3 direction = glm::normalize(acceleration);
                v = v - (v - direction * speed) * std::min(dt * friction, 1.0f);
            }
            if (!zero_acceleration)
            {
                const float max_input_speed = glm::length(v) > max_speed ? glm::length(v) : max_speed;
                v += acceleration * dt;
                const float l = glm::length(v);
                if (l > max_input_speed)
                    v *= max_input_speed / l;
            }
        }

        void on_locomotion_mode_changed(LocomotionCharacter& c, LocomotionMode previous, phys::PhysicsScene& physics)
        {
            apply_desired_stance(c, physics);
            if (c.locomotion_mode == LocomotionMode::grounded && previous == LocomotionMode::in_air)
            {
                // more friction for a moment: no sliding after landing (rolling on hard landings: Rhea-vhg.8)
                c.braking_friction_factor = c.locomotion.has_input ? 0.5f : 3.0f;
                c.braking_friction_reset_timer = landing_friction_time;
                // legs would twist into a spiral turning to the last input while landing
                c.locomotion.rotation_towards_last_input_direction_blocked = true;
            }
        }

        void update_character(LocomotionCharacter& c, const LocomotionInput& input, const anim::Animator* animator,
            float dt, phys::PhysicsScene& physics)
        {
            const LocomotionSettings& s = *c.settings;
            c.previous_position = c.position;
            c.previous_yaw = c.yaw;
            const glm::vec3 velocity_before = c.velocity;

            if (cv_log.get() && (input.toggle_walk || input.toggle_crouch || input.cycle_rotation_mode || input.roll))
                LogLocomotion.Log("Locomotion input: walk %d crouch %d rotation %d roll %d sprint %d", (int)input.toggle_walk,
                    (int)input.toggle_crouch, (int)input.cycle_rotation_mode, (int)input.roll, (int)input.sprint);

            // ---- desired states from the input (AAlsCharacterExample)
            if (input.toggle_walk)
                c.desired_gait = c.desired_gait == Gait::walking ? Gait::running : Gait::walking;
            if (input.sprint)
                c.desired_gait = Gait::sprinting;
            else if (c.desired_gait == Gait::sprinting)
                c.desired_gait = Gait::running;
            if (input.toggle_crouch)
                c.desired_stance = c.desired_stance == Stance::standing ? Stance::crouching : Stance::standing;
            if (input.set_rotation_mode)
                c.desired_rotation_mode = input.rotation_mode;
            else if (input.cycle_rotation_mode)
                c.desired_rotation_mode = c.desired_rotation_mode == RotationMode::velocity_direction
                    ? RotationMode::view_direction : RotationMode::velocity_direction;
            c.desired_aiming = input.aim;

            // ---- RefreshInput
            const glm::vec3 forward = yaw_to_direction(input.view_yaw);
            const glm::vec3 right = yaw_to_direction(input.view_yaw + 90.0f);
            glm::vec3 move = forward * input.move_axis.y + right * input.move_axis.x;
            if (glm::length(move) > 1.0f)
                move = glm::normalize(move);
            c.input_direction = move;
            c.locomotion.has_input = glm::dot(move, move) > 1e-6f;
            if (c.locomotion.has_input)
                c.locomotion.input_yaw = direction_to_yaw(move);

            // ---- RefreshLocomotionEarly
            c.locomotion.aiming_limit_applied_this_frame = false;

            // ---- RefreshView
            c.view.previous_yaw = c.view.yaw;
            c.view.yaw = unwind_degrees(input.view_yaw);
            c.view.pitch = input.view_pitch;
            if (dt > 1e-6f)
                c.view.yaw_speed = std::abs(unwind_degrees(c.view.yaw - c.view.previous_yaw)) / dt;

            // ---- RefreshLocomotion
            LocomotionState& l = c.locomotion;
            l.velocity = c.velocity;
            l.speed = glm::length(glm::vec2(c.velocity.x, c.velocity.z));
            l.has_velocity = l.speed >= c.scaled(0.01f);
            if (l.has_velocity)
                l.velocity_yaw = direction_to_yaw(c.velocity);
            // the desired velocity is the requested one (blocked by walls: still turn towards it)
            if (c.locomotion.has_input && c.locomotion_mode == LocomotionMode::grounded)
            {
                c.has_desired_velocity = true;
                c.desired_velocity_yaw = c.locomotion.input_yaw;
            }
            else
            {
                c.has_desired_velocity = l.has_velocity;
                c.desired_velocity_yaw = l.velocity_yaw;
            }
            l.moving = (l.has_input && l.has_velocity) || l.speed > c.scaled(s.moving_speed_threshold);

            // ---- RefreshGait, RefreshRotationMode, stance
            refresh_gait(c);
            refresh_rotation_mode(c);
            apply_desired_stance(c, physics);

            // ---- RefreshRotation
            refresh_grounded_rotation(c, dt, animator);
            refresh_in_air_rotation(c, dt);

            // ---- jump (AAlsCharacterExample::Input_OnJump: crouching stands up instead)
            bool jumped = false;
            if (input.jump && !c.jump_was_down && c.action == LocomotionAction::none && c.locomotion_mode == LocomotionMode::grounded)
            {
                if (c.stance == Stance::crouching)
                    c.desired_stance = Stance::standing;
                else
                {
                    jumped = true;
                    c.velocity.y = c.scaled(s.jump_velocity) / std::sqrt(std::max(c.size_scale, 1e-3f));
                    ++c.jump_count;
                }
            }
            c.jump_was_down = input.jump;

            // ---- movement (character movement component)
            if (c.braking_friction_reset_timer > 0.0f)
            {
                c.braking_friction_reset_timer -= dt;
                if (c.braking_friction_reset_timer <= 0.0f)
                    c.braking_friction_factor = 0.0f;
            }

            const phys::CharacterState before = physics.get_character_state(c.capsule);
            glm::vec3 horizontal(c.velocity.x, 0.0f, c.velocity.z);
            float vertical = c.velocity.y;
            const float brake_to_stop = c.scaled(0.1f);

            if (c.locomotion_mode == LocomotionMode::grounded && !jumped)
            {
                calc_velocity(horizontal, c.input_direction * c.max_acceleration, dt, c.ground_friction, c.braking_deceleration,
                    c.braking_friction_factor, c.max_speed, brake_to_stop);
                vertical = 0.0f;
            }
            else
            {
                // falling: air control, no braking
                float air_control = s.air_control;
                if (glm::length(horizontal) < c.scaled(0.25f))
                    air_control *= 2.0f;      // UE AirControlBoostMultiplier below AirControlBoostVelocityThreshold
                calc_velocity(horizontal, c.input_direction * c.scaled(s.air_acceleration) * air_control, dt, 0.0f, 0.0f, 0.0f,
                    c.max_speed, brake_to_stop);
                if (!jumped)
                    vertical -= s.gravity * dt;
            }

            glm::vec3 move_velocity = horizontal + glm::vec3(0.0f, vertical, 0.0f);
            if (c.locomotion_mode == LocomotionMode::grounded && !jumped)
            {
                // follow the ground (moving platforms); a gravity step keeps the capsule pressed to it
                move_velocity += before.ground_velocity;
                move_velocity.y -= s.gravity * dt;
            }

            const phys::CharacterState state = physics.move_character(c.capsule, move_velocity, dt);
            c.position = state.position;
            // velocity after collisions (relative to the ground)
            const glm::vec3 achieved = state.velocity - state.ground_velocity;
            c.velocity = glm::vec3(achieved.x, 0.0f, achieved.z);
            // the requested speed is kept along the free direction (walls cut it)
            if (glm::length(c.velocity) > glm::length(horizontal))
                c.velocity = horizontal;

            const bool on_ground = state.ground == phys::GroundState::on_ground;
            const LocomotionMode previous_mode = c.locomotion_mode;
            if (on_ground && !jumped && vertical <= 0.0f)
            {
                if (c.locomotion_mode == LocomotionMode::in_air)
                {
                    c.land_vertical_speed = vertical;
                    ++c.land_count;
                }
                c.locomotion_mode = LocomotionMode::grounded;
                c.velocity.y = 0.0f;
                c.air_time = 0.0f;
            }
            else
            {
                c.locomotion_mode = LocomotionMode::in_air;
                c.air_time += dt;
                c.velocity.y = vertical;
                // hit a ceiling
                if (vertical > 0.0f && state.velocity.y < vertical - 1e-3f)
                    c.velocity.y = std::max(state.velocity.y, 0.0f);
            }
            if (c.locomotion_mode != previous_mode)
                on_locomotion_mode_changed(c, previous_mode, physics);

            c.acceleration = dt > 1e-6f ? (c.velocity - velocity_before) / dt : glm::vec3(0.0f);

            // ---- RefreshLocomotionLate
            if (c.locomotion_mode == LocomotionMode::none || c.action != LocomotionAction::none)
                refresh_target_yaw_using_actor_rotation(c);
            l.reset_aiming_limit = !l.aiming_limit_applied_this_frame;
        }

        [[=ecs::system<ecs::Phase::Fixed>, =ecs::in_set<LocomotionSimulation>]]
        void update_locomotion_characters(ecs::Query<LocomotionCharacter, const LocomotionInput> characters,
            ecs::Query<const anim::Animator> animators, ecs::ResMut<phys::PhysicsScene> physics, ecs::Res<ecs::SimTime> time)
        {
            characters.each([&](ecs::Entity e, LocomotionCharacter& c, const LocomotionInput& input) {
                if (!c.settings || !c.capsule.is_valid())
                    return;
                const Gait gait = c.gait;
                const Stance stance = c.stance;
                const LocomotionMode mode = c.locomotion_mode;
                const RotationMode rotation = c.rotation_mode;
                update_character(c, input, animators.get(e), (float)time->dt, *physics);
                if (cv_log.get() && (gait != c.gait || stance != c.stance || mode != c.locomotion_mode || rotation != c.rotation_mode))
                    LogLocomotion.Log("Locomotion: %s, %s, %s, %s, speed %.2f m/s, vertical %.2f", to_string(c.locomotion_mode),
                        to_string(c.gait), to_string(c.stance), to_string(c.rotation_mode), c.locomotion.speed, c.velocity.y);
            });
        }

        // Render transform between the last two simulated ticks
        [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<LocomotionPresentation>, =ecs::ambiguous_with<SkyControl>]]
        void interpolate_locomotion_characters(ecs::Query<Transform, const LocomotionCharacter> characters, ecs::Res<ecs::FrameTime> time)
        {
            const float alpha = (float)time->alpha;
            characters.each([&](Transform& transform, const LocomotionCharacter& c) {
                transform.position = glm::mix(c.previous_position, c.position, alpha);
                transform.rotation = yaw_to_rotation(lerp_angle(c.previous_yaw, c.yaw, alpha));
            });
        }

        [[=ecs::on_remove]]
        void destroy_locomotion_capsule(ecs::Registry& registry, ecs::Entity, LocomotionCharacter& c)
        {
            if (phys::PhysicsScene* physics = registry.find_resource<phys::PhysicsScene>())
                physics->destroy_character(c.capsule);
        }

        ECS_REGISTER()
        SCENE_REGISTER_COMPONENTS(LocomotionInput, LocomotionPlayer, LocomotionCharacter)
    }

    bool init_locomotion_character(World& world, ecs::Entity e, const std::string& settings_path)
    {
        ecs::Registry& registry = world.registry;
        const std::string name = scene::get_name(registry, e);

        LocomotionCharacter c;
        c.settings = load_locomotion_settings(settings_path);
        if (!c.settings)
            return false;

        // size from the skeleton: pelvis height in the bind pose
        if (const SkinnedMesh* skinned = registry.get<SkinnedMesh>(e); skinned && skinned->pose)
        {
            const Skeleton& skeleton = skinned->get_skeleton();
            if (auto pelvis = skeleton.find_bone("pelvis"))
            {
                const BonePose bind = make_bind_pose(skeleton);
                glm::vec3 p = bind[*pelvis].translation;
                for (int32_t b = skeleton.bones[*pelvis].parent; b >= 0; b = skeleton.bones[b].parent)
                    p = bind[b].translation + bind[b].rotation * (bind[b].scale * p);
                p = glm::vec3(skeleton.root_transform * glm::vec4(p, 1.0f));
                if (p.y > 0.2f)
                    c.size_scale = p.y / c.settings->reference_pelvis_height;
            }
        }

        const Transform t = scene::get_world_transform(registry, e);
        c.position = c.previous_position = t.position.glm();
        c.yaw = c.previous_yaw = direction_to_yaw(t.rotation.glm() * glm::vec3(0, 0, 1));
        c.view.yaw = c.view.previous_yaw = c.yaw;
        set_target_yaw(c, c.yaw);
        refresh_gait_settings(c);

        c.capsule_height = c.scaled(c.settings->standing_height);
        c.capsule = world.get_physics().create_character({
            .radius = c.scaled(c.settings->capsule_radius),
            .height = c.capsule_height,
            .position = c.position + glm::vec3(0.0f, 0.05f, 0.0f),
            .user_data = e.bits(),
        });

        LogLocomotion.Log("Locomotion character '%s': size %.2f (pelvis), walk %.2f, run %.2f, sprint %.2f m/s, capsule %.2f x %.2f m",
            name.c_str(), c.size_scale, c.gait_settings.walk_forward_speed, c.gait_settings.run_forward_speed,
            c.gait_settings.sprint_speed, c.scaled(c.settings->capsule_radius), c.capsule_height);

        // looking where the character faces until a camera says otherwise
        registry.add<LocomotionInput>(e, LocomotionInput{ .view_yaw = c.yaw });
        registry.add<LocomotionCharacter>(e, std::move(c));
        return true;
    }

    bool debug_log_enabled()
    {
        return cv_log.get();
    }

    void set_scripted_locomotion_input(const std::optional<LocomotionInput>& input)
    {
        scripted_input = input;
    }

    Transform make_locomotion_camera(World& world, ecs::Entity character, float camera_yaw, float camera_pitch)
    {
        ecs::Registry& registry = world.registry;
        const LocomotionCharacter* c = registry.get<LocomotionCharacter>(character);
        const glm::vec3 position = scene::get_world_transform(registry, character).position.glm();
        const float scale = c ? c->size_scale : 1.0f;
        // the pivot follows the capsule height (crouching lowers the camera)
        const float pivot_height = camera_pivot_height * scale * (c ? std::clamp(c->capsule_height / c->scaled(c->settings->standing_height), 0.6f, 1.0f) : 1.0f);
        const glm::vec3 pivot = position + glm::vec3(0.0f, pivot_height, 0.0f);
        const glm::vec3 back = -camera_forward(camera_yaw, camera_pitch);

        float distance = camera_distance;
        LocomotionCameraProbe* probe = registry.find_resource<LocomotionCameraProbe>();
        if (!probe && c)
        {
            registry.set_resource<LocomotionCameraProbe>(world.get_physics().create_shape(phys::SphereShape{ camera_probe_radius }));
            probe = registry.find_resource<LocomotionCameraProbe>();
        }
        if (probe && c)
        {
            phys::PhysicsScene& physics = world.get_physics();
            const phys::BodyId body = physics.get_character_body(c->capsule);
            const phys::QueryFilter filter{
                .categories = phys::Category::static_world | phys::Category::voxel,
                .ignore_bodies = std::span(&body, 1),
            };
            if (auto hit = physics.sweep(probe->shape, { pivot }, back, camera_distance, filter))
                distance = std::max(hit->distance, 0.1f);
        }

        Transform t;
        t.position = pivot + back * distance;
        t.rotation = math::from_euler_rotation(glm::vec3(camera_pitch, camera_yaw, 0.0f));
        return t;
    }
}
