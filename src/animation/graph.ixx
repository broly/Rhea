export module animation:graph;

import std.compat;
import glm;
import assets;
import :core;
import :ops;
import :blend_space;
import :montage;

// Animation graphs written as code (an immediate mode DSL).
//
// A graph is a function that runs every frame and returns the pose: it calls node functions of a Graph,
// and composite nodes take their inputs as lambdas, so a branch whose weight is 0 is never evaluated:
//
//     anim::PoseRef fall(anim::Graph& g, FallNodes& n, float vertical_speed)
//     {
//         return g.blend(n.fast_alpha.update(vertical_speed, g.dt),
//             [&] { return g.play(n.fall, clips.fall, {.sync = "Fall"}); },
//             [&] { return g.play(n.fall_fast, clips.fall_fast, {.sync = "Fall"}); });
//     }
//
// What a node remembers between frames (playback time, smoothed inputs, the active state of a state
// machine, inertialization history) lives in node structs owned by the caller - usually members of the
// controller's component - and is passed in by reference. Plain blends and additives need no state.
//
// Semantics follow UE AnimGraph where ALS depends on them:
//  * a node is updated only while it contributes to the output (weight > 0) and keeps its time otherwise;
//  * entering a state of a state machine (or activating a child of a blend list with reset) resets the nodes
//    evaluated for it (UE re-initializes the state's subgraph);
//  * players of one sync group follow the group leader (highest weight): by sync markers when both clips
//    have them, else by normalized time;
//  * inertialization requests (inertialized transitions) go to the closest enclosing inertialize() node.
export namespace anim
{
    // Events of an update: clip notifies crossed by players (with the weight the player had), notifies of
    // montages, transition notifies of state machines.
    struct Event
    {
        std::string name;
        const AnimNotify* notify = nullptr;   // null for events that are not clip notifies
        float weight = 1.0f;
    };


    // ---------------------------------------------------------------- inputs

    // UE FInputScaleBiasClamp: maps an input (range / scale / bias / clamp) and optionally interpolates the
    // result towards it (speeds in 1/s, 0 = instant). Units of the ranges are the caller's.
    struct ScaleBiasClamp
    {
        bool map_range = false;
        float in_min = 0.0f, in_max = 1.0f;
        float out_min = 0.0f, out_max = 1.0f;
        float scale = 1.0f, bias = 0.0f;
        bool clamp = false;
        float clamp_min = 0.0f, clamp_max = 1.0f;
        bool interpolate = false;
        float speed_increasing = 10.0f, speed_decreasing = 10.0f;

        float value = 0.0f;
        bool initialized = false;

        float update(float input, float dt);
        void reset() { initialized = false; }
    };


    // ---------------------------------------------------------------- sync groups

    // Position of a sync group in its cycle, for the members to follow.
    struct SyncGroup
    {
        uint64_t advanced_frame = 0;   // frame the position was advanced for
        bool valid = false;
        float normalized = 0.0f;       // 0..1 of the leader clip
        std::string marker;            // last marker passed by the leader (empty: no markers)
        float marker_fraction = 0.0f;  // 0..1 towards the next marker

        struct Leader
        {
            const AnimationClip* clip = nullptr;
            float time = 0.0f;
            float rate = 0.0f;
            float weight = -1.0f;
            bool loop = true;
        };
        Leader leader;         // of this frame (highest weight so far)
        Leader last_leader;    // of the previous frame, drives the advance
    };

    struct SyncGroups
    {
        std::map<std::string, SyncGroup, std::less<>> groups;
    };


    // ---------------------------------------------------------------- node state

    // Base of stateful nodes: the frame they were last updated.
    struct NodeState
    {
        uint64_t last_frame = 0;
        bool updated_last_frame(uint64_t frame) const { return last_frame + 1 == frame; }
    };

    struct PlayParams
    {
        float rate = 1.0f;
        bool loop = true;
        float start = 0.0f;              // time set when the player (re)starts
        std::string_view sync = {};      // sync group name, empty: none
        bool sync_follower = false;      // never leads its group
        bool notifies = true;            // raise the clip's notifies as events
    };

    struct SequencePlayer : NodeState
    {
        float time = 0.0f;
        float previous_time = 0.0f;
        float weight = 0.0f;             // weight of the last update
        const AnimationClip* clip = nullptr;

        float remaining() const { return clip ? std::max(clip->duration - time, 0.0f) : 0.0f; }
        float normalized() const { return clip && clip->duration > 0.0f ? time / clip->duration : 0.0f; }
    };

    struct BlendSpacePlayer : NodeState
    {
        glm::vec2 input{0.0f};           // smoothed
        SyncGroup position;              // own cycle position when not in a sync group
        float weight = 0.0f;
        std::vector<float> sample_weights;
    };

    // Two way blend that restarts a child when it starts contributing (UE TwoWayBlend bResetChildOnActivation)
    struct TwoWayBlend : NodeState
    {
        bool a_relevant = false;
        bool b_relevant = false;
    };

    // Child selector with crossfades (UE Blend Poses by Int / Enum / Gameplay Tag)
    struct BlendList : NodeState
    {
        int32_t active = -1;
        std::vector<float> weights;      // current, per child
        std::vector<float> from;         // weights when the last switch started
        float elapsed = 0.0f;
        float duration = 0.0f;
        BlendShape curve;
        bool reset_on_activation = false;
        int32_t activated_frame_child = -1;
    };

    struct Transition
    {
        float duration = 0.2f;
        BlendShape curve;
        const BlendProfile* profile = nullptr;
        bool inertialize = false;        // switch at once and smooth it with the enclosing inertialize()
        std::string_view notify = {};    // event raised when the transition starts
    };

    // State machine over an enum. Transitions are decided by the caller (code, not rules): call transition()
    // before evaluating the machine with Graph::state_machine. Entering a state resets the nodes evaluated
    // for it unless the state is still blending out.
    struct StateMachineBase : NodeState
    {
        int32_t current = 0;
        int32_t previous = -1;
        float state_time = 0.0f;         // seconds in the current state
        uint64_t entered_frame = 0;
        bool reset_current = true;       // the current state starts fresh at its next evaluation

        struct Fading
        {
            int32_t state;
            float weight;                // at the start of the active transition
        };
        std::vector<Fading> fading;      // states blending out
        Transition transition_info;
        float transition_elapsed = 0.0f;
        float start_alpha = 0.0f;        // re-entering a state that was blending out starts from its weight
        bool inertialization_pending = false;
        std::string pending_notify;

        void start_transition(int32_t to, const Transition& t);
        float transition_alpha() const;  // linear 0..1
        // weight of the current state (UE GetInstanceStateWeight): 1 unless a transition into it is blending
        float current_weight() const;
        bool blending() const { return !fading.empty(); }
    };

    template <typename S>
    struct StateMachine : StateMachineBase
    {
        StateMachine(S initial = S{}) { current = (int32_t)initial; }

        S state() const { return (S)current; }
        S previous_state() const { return (S)previous; }
        bool in(S s) const { return current == (int32_t)s; }
        // true in the frame the state was entered
        bool just_entered(uint64_t frame) const { return entered_frame == frame; }

        // no-op when already in `to`
        void transition(S to, const Transition& t = {})
        {
            if ((int32_t)to != current)
                start_transition((int32_t)to, t);
        }
        // restart the current state too
        void force_transition(S to, const Transition& t = {}) { start_transition((int32_t)to, t); }
    };

    // Inertialization ("dead blending"): when a request arrives the last output keeps moving with its velocity
    // (decaying) and the new input fades in over the requested time. Cheap and smooth for instant switches.
    struct Inertialization : NodeState
    {
        // output of the last update and its velocity (per second)
        Pose last;
        std::vector<glm::vec3> linear_velocity;
        std::vector<glm::vec3> angular_velocity;     // rotation vector
        std::vector<float> curve_velocity;
        bool has_last = false;

        // the pose being faded out, extrapolated with decaying velocity
        Pose source;
        std::vector<glm::vec3> source_linear;
        std::vector<glm::vec3> source_angular;
        std::vector<float> source_curve;
        bool active = false;
        float elapsed = 0.0f;
        float duration = 0.0f;
        std::vector<float> bone_durations;           // per bone (blend profile), empty: `duration`

        // requests of this frame (the longest wins)
        float requested = -1.0f;
        const BlendProfile* requested_profile = nullptr;

        std::vector<uint32_t> filtered_curves;       // switch at once, never inertialized
        bool reset_on_becoming_relevant = true;
        float velocity_halflife = 0.05f;             // s, decay of the extrapolation
    };


    // ---------------------------------------------------------------- graph context

    class Graph
    {
    public:
        Graph(const Rig& rig, PosePool& pool, SyncGroups& sync, Slots& slots, std::vector<Event>& events,
            float dt, uint64_t frame);

        const Rig& rig;
        PosePool& pool;
        const float dt;
        const uint64_t frame;

        // ---- poses
        PoseRef new_pose() { return pool.acquire(); }
        PoseRef bind_pose();
        PoseRef additive_identity();
        PoseRef copy(const PoseRef& pose);

        // ---- leaves
        PoseRef play(SequencePlayer& node, const AnimationClip& clip, const PlayParams& params = {});
        PoseRef play(SequencePlayer& node, const AnimationClipHandle& clip, const PlayParams& params = {}) { return play(node, clip.get(), params); }
        // clip at a time, no state (UE Sequence Evaluator)
        PoseRef evaluate(const AnimationClip& clip, float time, bool loop = false);
        PoseRef evaluate(const AnimationClipHandle& clip, float time, bool loop = false) { return evaluate(clip.get(), time, loop); }
        PoseRef evaluate_frame(const AnimationClipHandle& clip, int32_t frame_index);
        // blend space played over time / evaluated at a normalized time (additive blend spaces too)
        PoseRef play(BlendSpacePlayer& node, const BlendSpace& space, glm::vec2 input, const PlayParams& params = {});
        PoseRef evaluate(BlendSpacePlayer& node, const BlendSpace& space, glm::vec2 input, float normalized_time);

        // ---- blends (children as lambdas returning PoseRef, evaluated only when they contribute)
        template <typename FA, typename FB>
        PoseRef blend(float alpha, FA&& a, FB&& b)
        {
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            if (alpha <= weight_epsilon)
                return weighted(1.0f, a);
            if (alpha >= 1.0f - weight_epsilon)
                return weighted(1.0f, b);
            PoseRef pa = weighted(1.0f - alpha, a);
            PoseRef pb = weighted(alpha, b);
            anim::blend(*pa, *pb, alpha, *pa);
            return pa;
        }
        PoseRef blend(PoseRef a, PoseRef b, float alpha);

        // blend whose children restart when they become relevant
        template <typename FA, typename FB>
        PoseRef blend(TwoWayBlend& node, float alpha, FA&& a, FB&& b)
        {
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            const bool reset = touch(node);
            const bool a_now = alpha < 1.0f - weight_epsilon;
            const bool b_now = alpha > weight_epsilon;
            const bool reset_a = a_now && (reset || !node.a_relevant);
            const bool reset_b = b_now && (reset || !node.b_relevant);
            node.a_relevant = a_now;
            node.b_relevant = b_now;
            return blend(alpha,
                [&] { ResetScope scope(*this, reset_a); return a(); },
                [&] { ResetScope scope(*this, reset_b); return b(); });
        }

        // normalized weights over n children (UE Multi Way Blend); child(i) -> PoseRef
        template <typename F>
        PoseRef blend_n(std::span<const float> weights, F&& child)
        {
            float total = 0.0f;
            for (float w : weights)
                total += std::max(w, 0.0f);
            if (total <= weight_epsilon)
                return weighted(1.0f, [&] { return child(0); });
            std::array<PoseRef, max_blend_children> poses;
            std::array<const Pose*, max_blend_children> pointers{};
            std::array<float, max_blend_children> normalized{};
            uint32_t count = 0;
            for (uint32_t i = 0; i < weights.size() && count < max_blend_children; ++i)
            {
                const float w = std::max(weights[i], 0.0f) / total;
                if (w <= weight_epsilon)
                    continue;
                poses[count] = weighted(w, [&] { return child(i); });
                pointers[count] = poses[count].operator->();
                normalized[count] = w;
                ++count;
            }
            if (count == 1)
                return std::move(poses[0]);
            PoseRef out = new_pose();
            blend_weighted(std::span(pointers.data(), count), std::span(normalized.data(), count), *out);
            return out;
        }

        // base + additive * alpha; the additive branch is skipped at alpha 0
        template <typename FA>
        PoseRef additive(PoseRef base, AdditiveKind kind, float alpha, FA&& additive_pose)
        {
            if (alpha <= weight_epsilon)
                return base;
            PoseRef add = weighted(alpha, additive_pose);
            apply_additive(rig, *base, *add, kind, std::min(alpha, 1.0f));
            return base;
        }

        // base with `layer` over the masked bones; the layer branch is skipped at alpha 0
        template <typename FL>
        PoseRef layered(PoseRef base, const BoneMask& mask, float alpha, FL&& layer, bool mesh_space_rotation = false,
            CurveBlend curves = CurveBlend::blend_by_weight)
        {
            if (alpha <= weight_epsilon)
                return base;
            PoseRef l = weighted(alpha, layer);
            layered_blend(rig, *base, *l, mask, std::min(alpha, 1.0f), mesh_space_rotation, curves);
            return base;
        }

        // ---- selectors
        template <typename F>
        PoseRef blend_list(BlendList& node, int32_t active, uint32_t count, float blend_time, F&& child,
            BlendShape curve = {})
        {
            update_blend_list(node, active, count, blend_time, curve);
            std::array<float, max_blend_children> w{};
            for (uint32_t i = 0; i < count && i < max_blend_children; ++i)
                w[i] = node.weights[i];
            return blend_n(std::span(w.data(), std::min<uint32_t>(count, max_blend_children)), [&](uint32_t i) {
                ResetScope scope(*this, (int32_t)i == node.activated_frame_child);
                return child(i);
            });
        }

        template <typename S, typename F>
        PoseRef state_machine(StateMachine<S>& sm, F&& state_pose)
        {
            begin_state_machine(sm);
            const float alpha = sm.transition_info.curve(sm.transition_alpha());
            PoseRef current;
            {
                ResetScope scope(*this, sm.reset_current);
                sm.reset_current = false;
                current = weighted(sm.blending() ? alpha : 1.0f, [&] { return state_pose((S)sm.current); });
            }
            if (!sm.blending())
                return current;

            // states blending out, weighted by what they had when the transition started
            std::array<float, max_blend_children> w{};
            const uint32_t n = std::min<uint32_t>((uint32_t)sm.fading.size(), max_blend_children);
            for (uint32_t i = 0; i < n; ++i)
                w[i] = sm.fading[i].weight;
            PoseRef out = weighted(1.0f - alpha, [&] {
                return blend_n(std::span(w.data(), n), [&](uint32_t i) { return state_pose((S)sm.fading[i].state); });
            });
            blend_transition(sm, *out, *current, alpha);
            return out;
        }

        // ---- inertialization
        template <typename F>
        PoseRef inertialize(Inertialization& node, F&& child)
        {
            inertialization_stack.push_back(&node);
            PoseRef pose = child();
            inertialization_stack.pop_back();
            return finish_inertialization(node, std::move(pose));
        }
        // to the closest enclosing inertialize(); ignored outside one
        void request_inertialization(float duration, const BlendProfile* profile = nullptr);

        // ---- montages
        // source with the montages playing in `slot` blended in (additive montages added)
        PoseRef slot(std::string_view slot_name, PoseRef source);
        // the same with a lazy source: not updated while montages cover it fully (UE Slot without "Always Update
        // Source Pose"), so the graph below becomes relevant again when they blend out
        template <typename F>
            requires std::invocable<F>
        PoseRef slot(std::string_view slot_name, F&& source)
        {
            const float source_weight = slots.source_weight(slot_name);
            PoseRef pose = source_weight > weight_epsilon ? weighted(source_weight, source) : bind_pose();
            return slot(slot_name, std::move(pose));
        }
        Slots& slots;

        // ---- curves
        void set_curve(PoseRef& pose, uint32_t curve, float value) { pose->curves[curve] = value; }

        // ---- context
        float weight() const { return current_weight; }
        bool resetting() const { return reset_depth > 0; }
        void raise(std::string name, const AnimNotify* notify = nullptr) { events.push_back({std::move(name), notify, current_weight}); }

        // Runs f with the weight of the branch multiplied by w (for sync leaders and notify weights)
        template <typename F>
        PoseRef weighted(float w, F&& f)
        {
            const float saved = current_weight;
            current_weight *= w;
            PoseRef pose = f();
            current_weight = saved;
            return pose;
        }

        // nodes evaluated inside start fresh (state entry)
        struct ResetScope
        {
            ResetScope(Graph& g, bool active) : g(g), active(active) { if (active) ++g.reset_depth; }
            ~ResetScope() { if (active) --g.reset_depth; }
            Graph& g;
            bool active;
        };

        // true when a node should start over (first update or inside a reset scope); marks it updated
        bool touch(NodeState& node);
        // true when the node was not updated last frame (UE OnBecomeRelevant call sites); marks it updated
        bool becoming_relevant(NodeState& node);

        static constexpr float weight_epsilon = 1e-4f;
        static constexpr uint32_t max_blend_children = 32;

    private:
        void update_blend_list(BlendList& node, int32_t active, uint32_t count, float blend_time, const BlendShape& curve);
        void begin_state_machine(StateMachineBase& sm);
        void blend_transition(StateMachineBase& sm, Pose& from_inout, const Pose& to, float alpha);
        PoseRef finish_inertialization(Inertialization& node, PoseRef pose);

        SyncGroup& sync_group(std::string_view name);
        float advance_player_time(SequencePlayer& node, const AnimationClip& clip, const PlayParams& params, bool reset);
        void raise_notifies(const AnimationClip& clip, float from, float to, bool looped);

        SyncGroups& sync;
        std::vector<Event>& events;
        float current_weight = 1.0f;
        int32_t reset_depth = 0;
        std::vector<Inertialization*> inertialization_stack;
    };


    // ---------------------------------------------------------------- sync helpers (also used by blend spaces)

    // position of `clip` at `time` -> group position (normalized and marker based)
    void set_sync_position(SyncGroup& group, const AnimationClip& clip, float time);
    // time in `clip` matching the group position
    float time_from_sync_position(const SyncGroup& group, const AnimationClip& clip);


    // ---------------------------------------------------------------- root motion

    // root bone of the clip a montage plays at montage time `time` (its motion, see anim::sample)
    BoneTransform root_transform(const Rig& rig, const Montage& montage, float time);
}
