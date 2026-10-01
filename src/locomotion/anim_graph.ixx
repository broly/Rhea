module locomotion:anim_graph;

import std.compat;
import glm;
import assets;
import animation;
import physics;
import :types;
import :settings;
import :character;
import :animation;

// What the parts of the locomotion animation graph share (not exported): the ALS clips, the update context,
// node helpers and the entry points of each graph part.
namespace loco
{
    // Clips, blend spaces of the ALS animation set (assets/animations/als, tools/ue_import/import_als.py)
    struct LocomotionClips
    {
        AnimationClipHandle stand_pose, crouch_pose;
        std::shared_ptr<const anim::BlendSpace> walk_run[6];      // F, B, LF, LB, RF, RB
        std::shared_ptr<const anim::BlendSpace> lean;
        AnimationClipHandle sprint, sprint_acceleration;
        AnimationClipHandle acceleration[4];                       // F, B, L, R (local additive)
        AnimationClipHandle walk[6];                               // F, B, LF, LB, RF, RB: foot plant poses of stops
        AnimationClipHandle rotate_left, rotate_right, crouch_rotate_left, crouch_rotate_right;
        AnimationClipHandle crouch_walk[6], crouch_walk_pose;
        AnimationClipHandle stand_to_crouch, crouch_to_stand;
        AnimationClipHandle stand_transition_left, stand_transition_right, crouch_transition_left, crouch_transition_right;
        AnimationClipHandle stand_dynamic_transition_left, stand_dynamic_transition_right;
        AnimationClipHandle crouch_dynamic_transition_left, crouch_dynamic_transition_right;
        AnimationClipHandle stop_left, stop_right;
        AnimationClipHandle turn_90_left, turn_90_right, turn_180_left, turn_180_right;
        AnimationClipHandle crouch_turn_90_left, crouch_turn_90_right, crouch_turn_180_left, crouch_turn_180_right;
        AnimationClipHandle roll;

        // in air
        AnimationClipHandle fall, fall_fast, flail;
        AnimationClipHandle jump_walk_left, jump_walk_right, jump_run_left, jump_run_right, jump_loop;
        AnimationClipHandle land_light, land_heavy, land_light_additive, land_heavy_additive;
        std::shared_ptr<const anim::BlendSpace> air_lean;

        // layering, view
        std::vector<OverlayDefinition> overlays;
        std::shared_ptr<const anim::BlendSpace> look;

        bool ok = true;

        AnimationClipHandle clip(const char* path);
        std::shared_ptr<const anim::BlendSpace> space(const char* path);
        bool load();
    };

    // everything the refresh functions and the graph need for one update
    struct Context
    {
        anim::Graph& g;
        LocomotionAnimation& a;
        const LocomotionCharacter& c;
        const AnimationSettings& s;
        const LocomotionClips& k;
        anim::Animator& animator;
        const phys::PhysicsScene& physics;
        phys::BodyId character_body;
    };

    inline constexpr std::string_view transition_slot = "Transition";
    inline constexpr std::string_view turn_in_place_standing_slot = "TurnInPlaceStanding";
    inline constexpr std::string_view turn_in_place_crouching_slot = "TurnInPlaceCrouching";
    inline constexpr std::string_view post_locomotion_slot = "PostLocomotion";

    inline float eval(const SampledCurve* c, float t, float fallback = 0.0f) { return c ? c->evaluate_x(t) : fallback; }

    // ALS units (cm of the mannequin) -> meters of this character
    inline float als_to_m(const LocomotionAnimation& a, float cm) { return cm * 0.01f * a.locomotion.scale; }
    // m/s of this character -> ALS cm/s
    inline glm::vec3 to_als_velocity(const LocomotionAnimation& a, const glm::vec3& v) { return v * (100.0f / std::max(a.locomotion.scale, 1e-3f)); }

    // actor space (x forward, y right) of a world vector
    glm::vec2 to_actor(const LocomotionAnimation& a, const glm::vec3& v);
    // acceleration / braking relative to the movement limits, actor space, length <= 1
    glm::vec2 acceleration_amount(const LocomotionAnimation& a);


    // ---------------------------------------------------------------- graph helpers

    // a pose evaluated once per update and handed out as copies (UE Save / Use Cached Pose)
    struct Cache
    {
        anim::PoseRef pose;
        bool done = false;
    };

    template <typename F>
    anim::PoseRef cached(anim::Graph& g, Cache& cache, F&& f)
    {
        if (!cache.done)
        {
            cache.pose = f();
            cache.done = true;
        }
        return g.copy(cache.pose);
    }

    inline void set_curve(anim::PoseRef& pose, uint32_t c, float v) { pose->curves[c] = v; }
    inline void scale_curve(anim::PoseRef& pose, uint32_t c, float v) { pose->curves[c] *= v; }
    // UE Modify Curve in blend mode
    inline void blend_curve(anim::PoseRef& pose, uint32_t c, float v, float alpha) { pose->curves[c] += (v - pose->curves[c]) * std::clamp(alpha, 0.0f, 1.0f); }

    // a state machine starts in its entry state when it becomes relevant (UE re-initializes it)
    template <typename S>
    bool enter_on_relevance(anim::Graph& g, anim::NodeState& relevance, anim::StateMachine<S>& sm, S entry)
    {
        if (!g.becoming_relevant(relevance))
            return false;
        sm.current = (int32_t)entry;
        sm.fading.clear();
        sm.reset_current = true;
        sm.state_time = 0.0f;
        sm.entered_frame = g.frame;
        return true;
    }

    // transition when the state's sequence player reached the end (UE automatic rule, trigger time 0)
    inline bool finished(const anim::SequencePlayer& p)
    {
        return p.clip && p.remaining() <= 1e-4f;
    }

    inline anim::Transition crossfade(float duration, anim::BlendShape curve = {}, const anim::BlendProfile* profile = nullptr)
    {
        return { .duration = duration, .curve = curve, .profile = profile };
    }

    inline anim::Transition inertial(float duration)
    {
        return { .duration = duration, .inertialize = true };
    }


    // ---------------------------------------------------------------- refresh functions shared by the graph parts

    void refresh_rotate_in_place(Context& x);
    void play_transition(Context& x, const AnimationClipHandle& clip, float blend_in, float blend_out, float play_rate,
        float start_time, bool from_standing_idle_only = false);
    void play_transition_left(Context& x, float blend_in, float blend_out, float rate, float start);
    void play_transition_right(Context& x, float blend_in, float blend_out, float rate, float start);
    void stop_transition_and_turn_in_place(Context& x, float blend_out = -1.0f);


    // ---------------------------------------------------------------- graph parts

    // AB_Als_Grounded (locomotion_animation.cpp)
    anim::PoseRef grounded_pose(Context& x);
    // AB_Als_Locomotion: in air states over the grounded pose with the Transition slot (animation_in_air.cpp)
    anim::PoseRef locomotion_pose(Context& x);

    // AB_Als_Layering with the overlay, AB_Als_Head (animation_layering.cpp)
    void init_layering(LocomotionAnimation& a, const anim::Rig& rig);
    void refresh_layering(LocomotionAnimation& a, const anim::Animator& animator);
    void refresh_view(Context& x);
    anim::PoseRef layering_pose(Context& x, anim::PoseRef locomotion);
    anim::PoseRef head_pose(Context& x, anim::PoseRef pose);

    // CR_Als and the feet state (animation_feet.cpp). model_to_world: model (skeleton root) space -> world
    void init_feet(LocomotionAnimation& a, const anim::Rig& rig);
    void apply_control_rig(Context& x, anim::Pose& pose, const glm::mat4& model_to_world);
    void refresh_dynamic_transitions(Context& x);
}
