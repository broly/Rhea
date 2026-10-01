export module animation:animator;

import std.compat;
import glm;
import assets;
import reflect;
import ecs;
import framework;
import :core;
import :graph;
import :montage;

// The ECS side of animation: an Animator component holds what every graph of an entity needs between
// frames (rig, pose pool, sync groups, montages, the output pose and its curves). A controller - a system of
// another module that knows the creature (character locomotion, a wolf, a door) - runs its graph on it:
//
//     [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<anim::Evaluate>]]
//     void animate_wolves(ecs::Query<anim::Animator, WolfAnimation, const WolfMovement> wolves, ecs::Res<ecs::FrameTime> time)
//     {
//         wolves.each([&](anim::Animator& animator, WolfAnimation& a, const WolfMovement& m) {
//             anim::Graph g = animator.begin_update((float)time->dt);
//             animator.end_update(g, wolf_graph(g, a, m));
//         });
//     }
//
// Systems of the sets run in this order inside Update: Evaluate (controllers) -> PostProcess (IK, look-at:
// they edit Animator::output) -> Apply (output -> SkinnedMesh).
export namespace anim
{
    struct Evaluate {};
    struct PostProcess {};
    struct Apply {};

    struct [[=scene::runtime_only]] Animator
    {
        std::shared_ptr<Rig> rig;
        PosePool pool;
        SyncGroups sync;
        Slots slots;

        Pose output;                      // local pose of the last update, with curves
        std::vector<Event> events;        // raised by the last update

        [[=rh::edit, =rh::read_only]] uint64_t frame = 0;
        [[=rh::edit]] float time_scale = 1.0f;
        [[=rh::edit]] bool paused = false;
        [[=rh::edit]] bool apply_to_mesh = true;   // Apply writes the output to the SkinnedMesh

        // name / value lines a controller fills for the Animation window
        std::vector<std::pair<std::string, std::string>> debug;

        // rig of the entity's skinned mesh, with the curves the controller's graph uses
        void init(std::shared_ptr<Rig> in_rig);
        bool valid() const { return rig != nullptr; }

        // starts a frame: advances montages, clears events
        Graph begin_update(float dt);
        // pose -> output
        void end_update(Graph& graph, PoseRef pose);

        float curve(uint32_t index) const { return index < output.curves.size() ? output.curves[index] : 0.0f; }
        bool has_event(std::string_view name) const;
        void debug_value(std::string name, std::string value) { debug.emplace_back(std::move(name), std::move(value)); }
    };

    // Animation window body for an entity (ImGui): curves, events, montages, sync groups, controller lines
    void draw_animator_debug(Animator& animator);
}
