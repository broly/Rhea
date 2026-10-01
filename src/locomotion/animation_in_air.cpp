module locomotion;

import std.compat;
import glm;
import assets;
import animation;
import physics;
import :anim_graph;

// AB_Als_Locomotion: grounded or in air (fall, jump, landing), and the in air refresh functions of
// UAlsAnimationInstance (RefreshInAir, RefreshGroundPrediction, RefreshInAirLean).
namespace loco
{
    namespace
    {
        using InAir = LocomotionAnimation::InAir;
        using State = InAir::State;
        using Jump = InAir::Jump;

        // walkable floor of UE character movement (44.765 degrees)
        constexpr float walkable_floor_cos = 0.71f;

        // How soon the character lands: the capsule swept along the velocity, 0..1 by the time to the hit
        void refresh_ground_prediction(Context& x)
        {
            LocomotionAnimation& a = x.a;
            AnimInAirState& s = a.in_air;
            constexpr float vertical_velocity_threshold = -200.0f;
            if (s.vertical_velocity > vertical_velocity_threshold || !a.ground_prediction_shape.is_valid())
            {
                s.ground_prediction = 0.0f;
                return;
            }
            const float allowance = 1.0f - clamp01(x.animator.curve(curve::GroundPredictionBlock));
            if (allowance <= 1e-4f)
            {
                s.ground_prediction = 0.0f;
                return;
            }

            constexpr float min_vertical_velocity = -4000.0f, max_vertical_velocity = -200.0f;
            glm::vec3 direction = to_als_velocity(a, a.locomotion.velocity);
            direction.y = std::clamp(direction.y, min_vertical_velocity, max_vertical_velocity);
            direction = glm::normalize(direction);
            const float distance = als_to_m(a, map_range_clamped(s.vertical_velocity, max_vertical_velocity, min_vertical_velocity, 150.0f, 2000.0f));

            // from the capsule center (the actor location of UE)
            const glm::vec3 start = a.locomotion.position + glm::vec3(0.0f, x.c.capsule_height * 0.5f, 0.0f);
            const phys::QueryFilter filter{ .ignore_bodies = std::span(&x.character_body, 1) };
            const std::optional<phys::Hit> hit = x.physics.sweep(a.ground_prediction_shape, { start }, direction, distance, filter);

            // the ground counts even when the sweep starts in it
            const bool ground_valid = hit && hit->normal.y >= walkable_floor_cos;
            s.ground_prediction = ground_valid ? eval(x.s.ground_prediction_amount, hit->distance / distance) * allowance : 0.0f;
        }

        // Lean by the velocity, reversed by the vertical speed (up -> down)
        void refresh_in_air_lean(Context& x)
        {
            LocomotionAnimation& a = x.a;
            constexpr float reference_speed = 350.0f;
            const glm::vec2 target = to_actor(a, to_als_velocity(a, a.locomotion.velocity)) / reference_speed
                * eval(x.s.in_air_lean_amount, a.in_air.vertical_velocity);
            if (a.pending_update || x.s.lean_interpolation_half_life <= 0.0f)
            {
                a.lean_right = target.y;
                a.lean_forward = target.x;
            }
            else
            {
                const float t = damper_exact_alpha(x.g.dt, x.s.lean_interpolation_half_life);
                a.lean_right = glm::mix(a.lean_right, target.y, t);
                a.lean_forward = glm::mix(a.lean_forward, target.x, t);
            }
        }

        void refresh_in_air(Context& x)
        {
            AnimInAirState& s = x.a.in_air;
            if (s.jumped)
            {
                s.jumped = false;
                s.jump_play_rate = lerp_clamped(1.2f, 1.5f, x.a.locomotion.speed_cm / 600.0f);
            }
            // kept after landing: how hard the landing was
            s.vertical_velocity = to_als_velocity(x.a, x.a.locomotion.velocity).y;
            refresh_ground_prediction(x);
            refresh_in_air_lean(x);
        }

        // The frame of a landing clip the legs reach for while the ground comes closer
        anim::PoseRef landing_pose(Context& x)
        {
            const float heavy_to_light = map_range_clamped(x.a.in_air.vertical_velocity, -1000.0f, -500.0f, 0.0f, 1.0f);
            return x.g.blend(heavy_to_light, [&] { return x.g.evaluate_frame(x.k.land_heavy, 0); }, [&] { return x.g.evaluate_frame(x.k.land_light, 0); });
        }

        // What Fall and Jump share around their body: the air lean additive, the landing pose by the ground
        // prediction, foot IK when about to land
        template <typename F>
        anim::PoseRef in_air_pose(Context& x, anim::BlendSpacePlayer& lean, anim::ScaleBiasClamp& prediction_alpha, F&& body)
        {
            anim::Graph& g = x.g;
            LocomotionAnimation& a = x.a;
            refresh_in_air(x);
            const AdditiveKind lean_kind = x.k.air_lean->additive ? x.k.air_lean->additive_kind : AdditiveKind::local;
            anim::PoseRef pose = g.blend(prediction_alpha.update(a.in_air.ground_prediction, g.dt),
                [&] {
                    return g.additive(body(), lean_kind, 1.0f,
                        [&] { return g.evaluate(lean, *x.k.air_lean, glm::vec2(a.lean_right, a.lean_forward), 0.0f); });
                },
                [&] { return landing_pose(x); });
            blend_curve(pose, curve::FootLeftIk, 1.0f, a.in_air.ground_prediction);
            blend_curve(pose, curve::FootRightIk, 1.0f, a.in_air.ground_prediction);
            set_curve(pose, curve::PoseStanding, 1.0f);
            set_curve(pose, curve::PoseInAir, 1.0f);
            return pose;
        }

        anim::PoseRef fall_pose(Context& x)
        {
            anim::Graph& g = x.g;
            InAir& n = x.a.in_air_nodes;
            return in_air_pose(x, n.fall_lean, n.fall_ground_prediction_alpha, [&] {
                const float vertical = x.a.in_air.vertical_velocity;
                return g.blend(n.flail_alpha.update(vertical, g.dt),
                    [&] {
                        return g.blend(n.fall_fast_alpha.update(vertical, g.dt),
                            [&] { return g.play(n.fall_fast, x.k.fall_fast, { .sync = "Fall" }); },
                            [&] { return g.play(n.fall, x.k.fall, { .sync = "Fall" }); });
                    },
                    [&] { return g.play(n.flail, x.k.flail); });
            });
        }

        anim::PoseRef jump_pose(Context& x)
        {
            anim::Graph& g = x.g;
            LocomotionAnimation& a = x.a;
            InAir& n = a.in_air_nodes;

            // Jump States: from the planted foot to the loop, then flailing
            enter_on_relevance(g, n.jump_relevance, n.jump, a.foot_planted <= 0.0f ? Jump::left_foot : Jump::right_foot);
            switch (n.jump.state())
            {
            case Jump::left_foot:
                if (finished(n.jump_left_alpha.value < 0.5f ? n.jump_walk_left : n.jump_run_left))
                    n.jump.transition(Jump::loop, inertial(0.2f));
                break;
            case Jump::right_foot:
                if (finished(n.jump_right_alpha.value < 0.5f ? n.jump_walk_right : n.jump_run_right))
                    n.jump.transition(Jump::loop, inertial(0.2f));
                break;
            case Jump::loop:
                n.jump.transition(Jump::flail, crossfade(1.0f));
                break;
            case Jump::flail:
                break;
            }

            return in_air_pose(x, n.jump_lean, n.jump_ground_prediction_alpha, [&] {
                const float rate = a.in_air.jump_play_rate;
                return g.inertialize(n.jump_inertialization, [&] {
                    return g.state_machine(n.jump, [&](Jump j) -> anim::PoseRef {
                        switch (j)
                        {
                        case Jump::left_foot:
                            return g.blend(n.jump_left_alpha.update(a.locomotion.speed_cm, g.dt),
                                [&] { return g.play(n.jump_walk_left, x.k.jump_walk_left, { .rate = rate, .loop = false, .start = 0.12f, .sync = "Jump" }); },
                                [&] { return g.play(n.jump_run_left, x.k.jump_run_left, { .rate = rate, .loop = false, .sync = "Jump" }); });
                        case Jump::right_foot:
                            return g.blend(n.jump_right_alpha.update(a.locomotion.speed_cm, g.dt),
                                [&] { return g.play(n.jump_walk_right, x.k.jump_walk_right, { .rate = rate, .loop = false, .start = 0.12f, .sync = "Jump" }); },
                                [&] { return g.play(n.jump_run_right, x.k.jump_run_right, { .rate = rate, .loop = false, .sync = "Jump" }); });
                        case Jump::loop:
                            return g.play(n.jump_loop, x.k.jump_loop, { .rate = rate, .sync = "Flail" });
                        case Jump::flail:
                            return g.play(n.jump_flail, x.k.flail, { .rate = 1.2f, .sync = "Flail" });
                        }
                        return g.bind_pose();
                    });
                });
            });
        }

        // the light or the heavy landing by the landing speed
        float heavy_landing(const LocomotionAnimation& a, float light, float heavy)
        {
            return map_range_clamped(a.in_air.vertical_velocity, light, heavy, 0.0f, 1.0f);
        }

        template <typename F>
        anim::PoseRef land_movement_pose(Context& x, F&& grounded)
        {
            anim::Graph& g = x.g;
            LocomotionAnimation& a = x.a;
            InAir& n = a.in_air_nodes;
            // the landing additive over the grounded pose: stronger after faster falls, half while aiming
            const float amount = std::clamp(map_range_clamped(a.in_air.vertical_velocity, 0.0f, -200.0f, 0.25f, 1.0f), 0.25f, 1.0f);
            return g.blend(amount, grounded, [&] {
                const float alpha = n.land_aiming_alpha.update(a.rotation_mode == RotationMode::aiming ? 1.0f : 0.0f, g.dt);
                return g.additive(grounded(), AdditiveKind::mesh_rotation, alpha, [&] {
                    return g.blend(heavy_landing(a, -750.0f, -1500.0f) * 0.75f,
                        [&] { return g.play(n.land_light_additive, x.k.land_light_additive, { .rate = 1.75f, .loop = false, .sync = "Land" }); },
                        [&] { return g.play(n.land_heavy_additive, x.k.land_heavy_additive, { .rate = 1.5f, .loop = false, .sync = "Land" }); });
                });
            });
        }

        anim::PoseRef land_pose(Context& x)
        {
            anim::Graph& g = x.g;
            LocomotionAnimation& a = x.a;
            InAir& n = a.in_air_nodes;
            refresh_rotate_in_place(x);
            anim::PoseRef pose = g.blend(heavy_landing(a, -500.0f, -1000.0f),
                [&] { return g.play(n.land_light, x.k.land_light, { .loop = false, .sync = "Land" }); },
                [&] { return g.play(n.land_heavy, x.k.land_heavy, { .loop = false, .sync = "Land" }); });
            for (uint32_t c : { curve::FootLeftIk, curve::FootRightIk, curve::FootLeftLock, curve::FootRightLock, curve::PoseStanding, curve::PoseGrounded })
                set_curve(pose, c, 1.0f);
            return pose;
        }

        // the landing clip of the higher weight finished (UE automatic transition rule)
        bool landing_finished(const anim::SequencePlayer& light, const anim::SequencePlayer& heavy)
        {
            return finished(heavy.weight > light.weight ? heavy : light);
        }
    }

    anim::PoseRef locomotion_pose(Context& x)
    {
        anim::Graph& g = x.g;
        LocomotionAnimation& a = x.a;
        InAir& n = a.in_air_nodes;

        if (g.becoming_relevant(n.relevance))
            a.lean_right = a.lean_forward = 0.0f;   // InitializeLean

        // transitions (UE Locomotion States): the aliases first
        const bool in_air = a.locomotion_mode == LocomotionMode::in_air;
        const bool grounded = a.locomotion_mode == LocomotionMode::grounded;
        const State before = n.states.state();
        const bool in_air_alias = before == State::grounded || before == State::land || before == State::land_movement;
        bool land_to_grounded = false;
        if (in_air_alias && in_air && a.in_air.jumped)
            n.states.transition(State::jump, inertial(0.1f));
        else if (in_air_alias && in_air)
            n.states.transition(State::fall, inertial(0.6f));
        else
        {
            switch (before)
            {
            case State::grounded:
                break;
            case State::fall:
            case State::jump:
                // the land conduit
                if (grounded)
                    n.states.transition(a.locomotion.has_input ? State::land_movement : State::land, inertial(0.1f));
                break;
            case State::land:
                if (a.locomotion.has_input || a.rotating_left || a.rotating_right || a.locomotion.speed_cm >= 650.0f)
                    n.states.transition(State::land_movement, crossfade(0.2f));
                else if (a.stance != Stance::standing)
                    n.states.transition(State::grounded, crossfade(0.2f));
                else if (landing_finished(n.land_light, n.land_heavy))
                {
                    n.states.transition(State::grounded, crossfade(0.8f, anim::BlendCurve::cubic, &a.quick_feet_profile));
                    land_to_grounded = true;
                }
                break;
            case State::land_movement:
                if (landing_finished(n.land_light_additive, n.land_heavy_additive))
                    n.states.transition(State::grounded, crossfade(0.3f));
                break;
            }
        }
        if (n.states.state() != before && before == State::grounded)
            stop_transition_and_turn_in_place(x);
        if (land_to_grounded)
            play_transition_right(x, 0.1f, 0.2f, 1.4f, 0.0f);   // AnimNotify_LandToGrounded

        // the grounded input with the Transition slot, shared by the states that use it
        Cache grounded_cache;
        auto grounded_input = [&] {
            return cached(g, grounded_cache, [&] { return g.slot(transition_slot, grounded_pose(x)); });
        };

        return g.inertialize(n.inertialization, [&] {
            return g.state_machine(n.states, [&](State s) -> anim::PoseRef {
                switch (s)
                {
                case State::grounded: return grounded_input();
                case State::fall: return fall_pose(x);
                case State::jump: return jump_pose(x);
                case State::land: return land_pose(x);
                case State::land_movement: return land_movement_pose(x, grounded_input);
                }
                return g.bind_pose();
            });
        });
    }
}
