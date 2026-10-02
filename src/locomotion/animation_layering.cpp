module locomotion;

import std.compat;
import glm;
import assets;
import animation;
import :anim_graph;

// AB_Als_Layering with the overlay (the data driven overlays: pose clips + idle secondary motion), AB_Als_Head and
// the view refresh functions of UAlsAnimationInstance (RefreshLayering, RefreshView, RefreshSpine, RefreshHead).
//
// Layering: the overlay is the base pose of each body part, the locomotion is added to it (as a dynamic additive
// over the stand / crouch base poses) by the Layer* curves that the overlay and locomotion clips carry.
namespace loco
{
    namespace
    {
        using Layering = LocomotionAnimation::Layering;

        constexpr float full_weight = 1.0f - 1e-5f;
        constexpr float relevant_weight = 1e-5f;

        // AB_Als_Rifle / AB_Als_PistolTwoHanded: relaxed (the weapon carried; arms swinging when running and sprinting
        // with the rifle), ready (raised, for 3 s after aiming or until sprinting / in air), aiming (the aim additive
        // over the view pitch, mesh space). Transitions as the state machine rules of the overlay graphs (the custom
        // aim in / out curves are cubic here). The locomotion actions (mantle, roll) keep the relaxed pose.
        anim::PoseRef weapon_overlay_pose(Context& x, uint32_t index)
        {
            anim::Graph& g = x.g;
            LocomotionAnimation& a = x.a;
            Layering& n = a.layering_nodes;
            const OverlayDefinition& o = x.k.overlays[index];
            const WeaponOverlay& w = *o.weapon;
            Layering::WeaponNodes& wn = n.overlay_weapon[index];

            enter_on_relevance(g, wn.relevance, wn.states, WeaponOverlayState::relaxed);
            const bool aiming = a.rotation_mode == RotationMode::aiming;
            const bool in_air = a.locomotion_mode == LocomotionMode::in_air;
            const bool sprinting = a.pose.gait_sprinting > 0.5f;
            switch (wn.states.state())
            {
                case WeaponOverlayState::relaxed:
                    if (aiming)
                        wn.states.transition(WeaponOverlayState::ready, crossfade(0.2f));
                    break;
                case WeaponOverlayState::ready:
                    if (aiming)
                        wn.states.transition(WeaponOverlayState::aiming, crossfade(0.2f, anim::BlendCurve::cubic));
                    else if (in_air || sprinting)
                        wn.states.transition(WeaponOverlayState::relaxed, crossfade(0.2f));
                    else if (wn.states.state_time >= 3.0f)
                        wn.states.transition(WeaponOverlayState::relaxed, crossfade(0.75f, anim::BlendCurve::cubic));
                    break;
                case WeaponOverlayState::aiming:
                    if (!aiming)
                        wn.states.transition(WeaponOverlayState::ready, crossfade(1.0f, anim::BlendCurve::cubic));
                    break;
            }

            auto frame = [&](int32_t f) { return g.evaluate_frame(o.poses, f); };
            auto stances = [&](auto&& stand, auto&& crouch) {
                const float stance_weights[2] = { a.pose.standing, a.pose.crouching };
                return g.blend_n(std::span(stance_weights), [&](uint32_t i) { return i == 0 ? stand() : crouch(); });
            };
            auto relaxed = [&] {
                // walking -> running (rifle: the run arms, held as walking when backwards / sideways) -> sprinting
                auto moving = [&] {
                    auto sprint = [&] {
                        if (w.sprint_arms.is_valid())
                            return g.play(wn.sprint_arms, w.sprint_arms, { .sync = "Movement", .sync_follower = true });
                        return frame(w.relaxed_sprint >= 0 ? w.relaxed_sprint : w.relaxed_walk);
                    };
                    if (!w.run_arms.is_valid())
                        return g.blend(a.pose.gait_sprinting, [&] { return frame(w.relaxed_walk); }, sprint);
                    auto run = [&] {
                        const float directions[4] = { a.grounded.forward, a.grounded.backward, a.grounded.left, a.grounded.right };
                        return g.blend_n(std::span(directions), [&](uint32_t i) {
                            return i == 0 ? g.play(wn.run_arms, w.run_arms, { .sync = "Movement", .sync_follower = true }) : frame(w.relaxed_walk);
                        });
                    };
                    return g.blend(a.pose.gait_running, [&] { return frame(w.relaxed_walk); },
                        [&] { return g.blend(a.pose.gait_sprinting, run, sprint); });
                };
                auto grounded = [&] {
                    return stances([&] { return g.blend(a.pose.gait_walking, [&] { return frame(w.relaxed_stand); }, moving); },
                        [&] { return frame(w.relaxed_crouch); });
                };
                anim::PoseRef pose = w.air < 0 ? grounded() : g.blend(a.pose.in_air, grounded, [&] {
                    return g.blend(n.overlay_ground_prediction[index].update(a.in_air.ground_prediction, g.dt),
                        [&] { return frame(w.air); }, [&] { return frame(w.air_landing >= 0 ? w.air_landing : w.air); });
                });
                return pose;
            };

            auto ready = [&] {
                return stances(
                    [&] { return g.blend(a.pose.gait_walking, [&] { return frame(w.ready_stand); }, [&] { return frame(w.ready_walk); }); },
                    [&] { return frame(w.ready_crouch); });
            };

            auto aim = [&] {
                // ExplicitTime = PitchAmount (0 down .. 1 up) of clips one second long
                auto aim_at = [&](const AnimationClipHandle& clip) {
                    return g.evaluate(clip, a.view.pitch_amount * clip.get().duration);
                };
                return stances(
                    [&] {
                        anim::PoseRef base = g.blend(a.pose.gait_walking, [&] { return frame(w.aim_stand); }, [&] { return frame(w.aim_walk); });
                        return g.additive(std::move(base), AdditiveKind::mesh_rotation, 1.0f, [&] { return aim_at(w.aim_clip); });
                    },
                    [&] {
                        return g.additive(frame(w.aim_crouch), AdditiveKind::mesh_rotation, 1.0f, [&] { return aim_at(w.aim_crouch_clip); });
                    });
            };

            anim::PoseRef pose = g.state_machine(wn.states, [&](WeaponOverlayState state) -> anim::PoseRef {
                switch (state)
                {
                    case WeaponOverlayState::ready: return ready();
                    case WeaponOverlayState::aiming: return aim();
                    default: return relaxed();
                }
            });
            // secondary motion once (its player must not advance twice while two states blend): less while aiming
            const float idle_alpha = wn.states.in(WeaponOverlayState::aiming) ? 0.5f : 0.75f;
            return g.additive(std::move(pose), AdditiveKind::local, idle_alpha,
                [&] { return g.play(n.overlay_idle[index], o.idle, { .sync = "Secondary Motion" }); });
        }

        // the poses clip of an overlay: 0 standing, 1 standing walking (and in air), 2 crouching
        anim::PoseRef overlay_pose(Context& x, uint32_t index)
        {
            anim::Graph& g = x.g;
            LocomotionAnimation& a = x.a;
            Layering& n = a.layering_nodes;
            const OverlayDefinition& o = x.k.overlays[index];
            if (o.weapon)
                return weapon_overlay_pose(x, index);

            auto grounded = [&] {
                const float stance_weights[2] = { a.pose.standing, a.pose.crouching };
                return g.blend_n(std::span(stance_weights), [&](uint32_t i) {
                    if (i == 1)
                        return g.evaluate_frame(o.poses, 2);
                    return g.blend(a.pose.gait_walking, [&] { return g.evaluate_frame(o.poses, 0); }, [&] { return g.evaluate_frame(o.poses, 1); });
                });
            };
            auto in_air = [&] {
                return g.blend(n.overlay_ground_prediction[index].update(a.in_air.ground_prediction, g.dt),
                    [&] { return g.evaluate_frame(o.poses, 1); }, [&] { return g.evaluate_frame(o.poses, 0); });
            };
            anim::PoseRef pose = g.blend(a.pose.in_air, grounded, in_air);
            return g.additive(std::move(pose), AdditiveKind::local, o.idle_alpha,
                [&] { return g.play(n.overlay_idle[index], o.idle, { .sync = "Secondary Motion" }); });
        }

        // RefreshSpine: the spine turns towards the view while aiming, and back smoothly when it stops
        void refresh_spine(Context& x, float spine_blend)
        {
            LocomotionAnimation& a = x.a;
            AnimSpineState& sp = a.spine;
            const float dt = x.g.dt;
            const bool allowed = a.rotation_mode == RotationMode::aiming || a.view_mode == ViewMode::first_person;
            if (sp.rotation_allowed != allowed)
            {
                sp.rotation_allowed = allowed;
                // remap the amount so that the current angle stays where it is
                if (allowed)
                {
                    if (sp.amount >= full_weight)
                    {
                        sp.amount_scale = 1.0f;
                        sp.amount_bias = 0.0f;
                    }
                    else
                    {
                        sp.amount_scale = 1.0f / (1.0f - sp.amount);
                        sp.amount_bias = -sp.amount * sp.amount_scale;
                    }
                }
                else
                {
                    sp.amount_scale = sp.amount > relevant_weight ? 1.0f / sp.amount : 1.0f;
                    sp.amount_bias = 0.0f;
                }
                sp.last_yaw_angle = sp.yaw_angle;
                sp.last_yaw_angle_world = a.locomotion.yaw;
            }

            if (allowed)
            {
                if (a.pending_update || sp.amount >= full_weight)
                {
                    sp.amount = 1.0f;
                    sp.yaw_angle = a.view.yaw_angle;
                }
                else
                {
                    sp.amount = glm::mix(sp.amount, 1.0f, damper_exact_alpha(dt, 0.1f));
                    sp.yaw_angle = lerp_angle(sp.last_yaw_angle, a.view.yaw_angle, sp.amount * sp.amount_scale + sp.amount_bias);
                }
            }
            else if (a.pending_update || sp.amount <= relevant_weight)
            {
                sp.amount = 0.0f;
                sp.yaw_angle = 0.0f;
            }
            else
            {
                // faster when the camera turns quickly, else the spine lags behind the body
                constexpr float reference_view_yaw_speed = 40.0f;
                const float multiplier = a.view.yaw_speed > reference_view_yaw_speed ? reference_view_yaw_speed / a.view.yaw_speed : 1.0f;
                sp.amount = glm::mix(sp.amount, 0.0f, damper_exact_alpha(dt, 0.7f * multiplier));
                // keep the spine rotation in world space, at most 30 degrees behind the body
                const float offset = std::clamp(unwind_degrees(sp.last_yaw_angle_world - a.locomotion.yaw), -30.0f, 30.0f);
                sp.last_yaw_angle_world = unwind_degrees(offset + a.locomotion.yaw);
                sp.yaw_angle = lerp_angle(0.0f, sp.last_yaw_angle + offset, sp.amount * sp.amount_scale + sp.amount_bias);
            }
            sp.final_yaw_angle = lerp_angle(0.0f, sp.yaw_angle, spine_blend);
        }

        // UE SpringMath::CriticalSpringDamper with a half life
        void critical_spring_damper(float& x, float& v, float target, float half_life, float dt)
        {
            const float y = 2.0f * 0.69314718f / std::max(half_life, 1e-5f);
            const float j0 = x - target;
            const float j1 = v + j0 * y;
            const float e = std::exp(-y * dt);
            x = e * (j0 + j1 * dt) + target;
            v = e * (v - j1 * y * dt);
        }

        // RefreshHead: where the head looks (view, or the input direction in velocity direction mode)
        void refresh_head(Context& x)
        {
            LocomotionAnimation& a = x.a;
            const AnimationSettings& s = x.s;
            AnimHeadState& h = a.head;
            const float dt = x.g.dt;
            const bool first_person = a.view_mode == ViewMode::first_person;

            // independent of the character rotation; clamped (not unwound) so the head does not switch sides
            h.yaw_angle = std::clamp(h.yaw_angle - a.locomotion.yaw_velocity * dt, -180.0f, 180.0f);

            float target_pitch = 0.0f, target_yaw = 0.0f;
            bool ready_to_switch_sides = false;
            constexpr float counter_clockwise_threshold = 5.0f;
            if (a.rotation_mode == RotationMode::velocity_direction)
            {
                target_yaw = unwind_degrees((a.locomotion.has_input ? a.locomotion.input_yaw : a.locomotion.target_yaw) - a.locomotion.yaw);
                if (std::abs(a.locomotion.yaw_velocity) > 20.0f)
                    target_yaw = (a.locomotion.yaw_velocity > 0.0f ? 1.0f : -1.0f) * std::abs(target_yaw);   // with the body
                else if (std::abs(target_yaw) > 180.0f - counter_clockwise_threshold)
                    target_yaw = h.yaw_angle;   // about to turn around: could start the wrong way
                h.switching_look_sides = false;
            }
            else
            {
                target_pitch = a.view.pitch_angle;
                target_yaw = std::clamp(a.view.yaw_angle, -180.0f + counter_clockwise_threshold, 180.0f - counter_clockwise_threshold);
                if (!first_person)
                {
                    // looking far over one shoulder: stay on that side for a while
                    if (h.yaw_angle >= 90.0f && target_yaw <= -160.0f)
                    {
                        target_yaw = 180.0f - counter_clockwise_threshold;
                        ready_to_switch_sides = !a.locomotion.has_input;
                    }
                    else if (h.yaw_angle <= -90.0f && target_yaw >= 160.0f)
                    {
                        target_yaw = -180.0f + counter_clockwise_threshold;
                        ready_to_switch_sides = !a.locomotion.has_input;
                    }
                }
            }

            if (h.initialization_required)
            {
                h.initialization_required = false;
                h.pitch_angle = target_pitch;
                h.yaw_angle = target_yaw;
                h.yaw_velocity = 0.0f;
                h.switching_look_sides = false;
            }
            else
            {
                h.pitch_angle = damper_exact_angle(h.pitch_angle, target_pitch, dt,
                    first_person ? s.head_first_person_pitch_angle_half_life : s.head_pitch_angle_half_life);
                if (ready_to_switch_sides)
                    h.switching_look_sides = true;
                else if (std::abs(unwind_degrees(target_yaw - h.yaw_angle)) <= 10.0f)
                    h.switching_look_sides = false;
                const float half_life = h.switching_look_sides ? s.head_switch_look_sides_yaw_angle_half_life
                    : first_person ? s.head_first_person_yaw_angle_half_life : s.head_yaw_angle_half_life;
                critical_spring_damper(h.yaw_angle, h.yaw_velocity, target_yaw, half_life, dt);
            }
            h.yaw_amount = h.yaw_angle / 360.0f + 0.5f;
        }
    }

    void init_layering(LocomotionAnimation& a, const anim::Rig& rig)
    {
        Layering& n = a.layering_nodes;
        n.pelvis = rig.make_mask({ { "pelvis" }, { "thigh_l", -1 }, { "thigh_r", -1 } });
        n.spine = rig.make_mask({ { "spine_01", 3 } });
        n.neck = rig.make_mask({ { "neck_01" } });
        n.arm_left = rig.make_mask({ { "clavicle_l", 1 } });
        n.arm_right = rig.make_mask({ { "clavicle_r", 1 } });
        n.hand_left = rig.make_mask({ { "index_01_l" }, { "middle_01_l" }, { "ring_01_l" }, { "pinky_01_l" }, { "thumb_01_l" } });
        n.hand_right = rig.make_mask({ { "index_01_r" }, { "middle_01_r" }, { "ring_01_r" }, { "pinky_01_r" }, { "thumb_01_r" } });
        const size_t overlays = a.clips ? a.clips->overlays.size() : 0;
        n.overlay_idle.resize(overlays);
        n.overlay_ground_prediction.assign(overlays, anim::ScaleBiasClamp{ .interpolate = true, .speed_increasing = 20.0f, .speed_decreasing = 5.0f });
        n.overlay_weapon.clear();
        n.overlay_weapon.resize(overlays);
    }

    void refresh_layering(LocomotionAnimation& a, const anim::Animator& animator)
    {
        auto c = [&](uint32_t i) { return animator.curve(i); };
        AnimLayeringState& l = a.layering;
        l.head = c(curve::LayerHead);
        l.head_additive = c(curve::LayerHeadAdditive);
        l.head_slot = c(curve::LayerHeadSlot);
        // the mesh space blend is 1 unless the local space blend is 1
        l.arm_left = c(curve::LayerArmLeft);
        l.arm_left_additive = c(curve::LayerArmLeftAdditive);
        l.arm_left_slot = c(curve::LayerArmLeftSlot);
        l.arm_left_local_space = c(curve::LayerArmLeftLocalSpace);
        l.arm_left_mesh_space = l.arm_left_local_space >= full_weight ? 0.0f : 1.0f;
        l.arm_right = c(curve::LayerArmRight);
        l.arm_right_additive = c(curve::LayerArmRightAdditive);
        l.arm_right_slot = c(curve::LayerArmRightSlot);
        l.arm_right_local_space = c(curve::LayerArmRightLocalSpace);
        l.arm_right_mesh_space = l.arm_right_local_space >= full_weight ? 0.0f : 1.0f;
        l.hand_left = c(curve::LayerHandLeft);
        l.hand_right = c(curve::LayerHandRight);
        l.spine = c(curve::LayerSpine);
        l.spine_additive = c(curve::LayerSpineAdditive);
        l.spine_slot = c(curve::LayerSpineSlot);
        l.pelvis = c(curve::LayerPelvis);
        l.pelvis_slot = c(curve::LayerPelvisSlot);
        l.legs = c(curve::LayerLegs);
        l.legs_slot = c(curve::LayerLegsSlot);
    }

    void refresh_view(Context& x)
    {
        LocomotionAnimation& a = x.a;
        if (a.action == LocomotionAction::none)
        {
            a.view.yaw_angle = unwind_degrees(a.view.yaw_world - a.locomotion.yaw);
            a.view.pitch_angle = unwind_degrees(a.view.pitch_world);
            a.view.pitch_amount = 0.5f - a.view.pitch_angle / 180.0f;
        }
        const float view_amount = 1.0f - clamp01(x.animator.curve(curve::ViewBlock));
        const float aiming_amount = clamp01(x.animator.curve(curve::PoseAiming));
        a.view.head_blend = view_amount * (1.0f - aiming_amount);
        refresh_spine(x, view_amount * aiming_amount);
    }

    anim::PoseRef layering_pose(Context& x, anim::PoseRef locomotion)
    {
        anim::Graph& g = x.g;
        LocomotionAnimation& a = x.a;
        Layering& n = a.layering_nodes;
        const AnimLayeringState& l = a.layering;
        const anim::Rig& rig = g.rig;
        const uint32_t overlay_count = (uint32_t)x.k.overlays.size();
        if (overlay_count == 0)
            return locomotion;

        // the overlay (switched with a crossfade, smoothed by inertialization)
        Cache overlay_cache;
        auto overlay = [&] {
            return cached(g, overlay_cache, [&] {
                return g.inertialize(n.overlay_inertialization, [&] {
                    return g.blend_list(n.overlays, std::clamp(a.overlay, 0, (int32_t)overlay_count - 1), overlay_count, 0.2f,
                        [&](uint32_t i) { return overlay_pose(x, i); });
                });
            });
        };

        // the locomotion as additives over the stand / crouch base poses: mesh space for the body, local space for
        // the arms (their motion then follows the rotation the spine gets)
        const float stance_weights[2] = { a.pose.standing, a.pose.crouching };
        anim::PoseRef base = g.blend_n(std::span(stance_weights), [&](uint32_t i) { return g.evaluate(i == 0 ? x.k.stand_pose : x.k.crouch_pose, 0.0f); });
        anim::PoseRef additive_mesh = g.new_pose();
        anim::PoseRef additive_local = g.new_pose();
        anim::make_additive(rig, *locomotion, *base, AdditiveKind::mesh_rotation, *additive_mesh);
        anim::make_additive(rig, *locomotion, *base, AdditiveKind::local, *additive_local);

        // a body part: the overlay (or its layering slot) with the locomotion added, over the plain locomotion
        auto part = [&](float amount, float slot_amount, std::string_view slot, float additive_amount, bool mesh_space) {
            return g.blend(amount, [&] { return g.copy(locomotion); }, [&] {
                anim::PoseRef pose = g.blend(slot_amount, overlay, [&] { return g.slot(slot, overlay()); });
                if (additive_amount > relevant_weight)
                    anim::apply_additive(rig, *pose, mesh_space ? *additive_mesh : *additive_local,
                        mesh_space ? AdditiveKind::mesh_rotation : AdditiveKind::local, std::min(additive_amount, 1.0f));
                return pose;
            });
        };
        Cache arm_left_cache, arm_right_cache;
        auto arm_left = [&] { return cached(g, arm_left_cache, [&] { return part(l.arm_left, l.arm_left_slot, "ArmLeft", l.arm_left_additive, false); }); };
        auto arm_right = [&] { return cached(g, arm_right_cache, [&] { return part(l.arm_right, l.arm_right_slot, "ArmRight", l.arm_right_additive, false); }); };

        // legs, pelvis, spine, head and arms in mesh space, arms and hands in local space
        anim::PoseRef pose = part(l.legs, l.legs_slot, "Legs", 1.0f, true);
        pose = g.layered(std::move(pose), n.pelvis, 1.0f, [&] { return part(l.pelvis, l.pelvis_slot, "Pelvis", 1.0f, true); }, true);
        pose = g.layered(std::move(pose), n.spine, 1.0f, [&] { return part(l.spine, l.spine_slot, "Spine", l.spine_additive, true); }, true);
        pose = g.layered(std::move(pose), n.neck, 1.0f, [&] { return part(l.head, l.head_slot, "Head", l.head_additive, true); }, true);
        pose = g.layered(std::move(pose), n.arm_left, l.arm_left_mesh_space, arm_left, true);
        pose = g.layered(std::move(pose), n.arm_right, l.arm_right_mesh_space, arm_right, true);
        pose = g.layered(std::move(pose), n.arm_left, l.arm_left_local_space, arm_left);
        pose = g.layered(std::move(pose), n.arm_right, l.arm_right_local_space, arm_right);
        pose = g.layered(std::move(pose), n.hand_left, l.hand_left, overlay);
        pose = g.layered(std::move(pose), n.hand_right, l.hand_right, overlay);

        // curves straight from the locomotion and the overlay (the Curves slot can change them), not from the layering
        anim::PoseRef curves = g.slot("Curves", [&] {
            anim::PoseRef o = overlay();
            for (uint32_t c : { curve::LayerHeadSlot, curve::LayerArmLeftSlot, curve::LayerArmRightSlot, curve::LayerSpineSlot,
                     curve::LayerPelvisSlot, curve::LayerLegsSlot })
                set_curve(o, c, 0.0f);
            return o;
        });
        for (size_t c = 0; c < pose->curves.size(); ++c)
            pose->curves[c] = locomotion->curves[c] + curves->curves[c];
        return pose;
    }

    anim::PoseRef head_pose(Context& x, anim::PoseRef pose)
    {
        anim::Graph& g = x.g;
        LocomotionAnimation& a = x.a;
        Layering& n = a.layering_nodes;
        if (!x.k.look)
            return pose;
        return g.additive(std::move(pose), AdditiveKind::mesh_rotation, a.view.head_blend, [&] {
            if (g.becoming_relevant(n.head_relevance))
                a.head.initialization_required = true;   // InitializeHead
            refresh_head(x);
            return g.evaluate(n.look, *x.k.look, glm::vec2(a.head.pitch_angle, 0.0f), a.head.yaw_amount);
        });
    }
}
