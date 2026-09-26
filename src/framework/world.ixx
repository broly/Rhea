module;

#include <json/value.h>

export module framework:world;

import std.compat;

import :engine_clock;
import :core;
import :scene;
import dependency_collector;
import rhmath;
import rhobject;
import physics;
import ecs;
import name;


// The simulation: ECS entities with their components and systems, physics, legacy world scripts.
//
// Frame (tick): fixed ticks [FixedPre, Fixed, FixedPost (physics step)] x N, Update, world scripts, Late
// (transform propagation, render sync). Content comes from level / prefab JSON:
//
//     { "name": "cube", "prefab": "prefabs/cube.json",
//       "components": { "Transform": { "position": {...} }, "MeshRenderer": { "materials": [...] } },
//       "children": [ { "name": "...", "components": {...} } ] }
//
// Components are applied in order prefab -> entry, each JSON only overrides the fields it names
// (see ComponentType::load). Children of an entry with the same name as a prefab child override it.
export class World : public std::enable_shared_from_this<World>
{
public:
    World();

    void tick();
    void init();

    bool load_bootstrap_level();
    bool load_level(std::string level_path);

    // --- entities ---

    // Plain entity with Name and Transform
    ecs::Entity spawn(Name name, const Transform& transform = {});

    // Entity (tree) from a prefab file, e.g. "prefabs/cube.json"; waits for its assets, runs PostLoad
    ecs::Entity spawn_prefab(std::string_view prefab_path, Name name = {}, std::optional<Transform> transform = std::nullopt);

    // First entity with this Name (null if none)
    ecs::Entity find_entity(Name name) const;

    // Destroys the entity and its ChildOf descendants
    void destroy_recursive(ecs::Entity e);

    // --- scripts ---

    template<typename T>
    void add_script()
    {
        auto script = std::make_unique<T>();
        scripts.push_back(std::move(script));
    }

    double get_time_seconds() const;
    double get_delta_seconds() const;
    double get_avg_fps() const;

    void set_clock(std::shared_ptr<EngineClock> in_clock)
    {
        clock = in_clock;
    }

    phys::PhysicsScene& get_physics() const { return *physics; }

    // ECS simulation: entities / components / resources, and the systems that run on them.
    // Engine services are resources too (set_resource_ref): systems take them as parameters,
    // e.g. ResMut<phys::PhysicsScene>, Res<Input>, ResMut<SceneView> - never capture them.
    ecs::Registry registry;
    ecs::Schedule schedule;

    std::vector<std::unique_ptr<WorldScript>> scripts;
    std::unique_ptr<phys::PhysicsScene> physics;
    std::shared_ptr<EngineClock> clock;
    DependencyCollector collector;

    static constexpr int32_t NUM_SAMPLES_AVG_FPS = 60;
    static constexpr bool COUNT_AVG_FPS = true;

    // AVG FPS
    std::array<double, NUM_SAMPLES_AVG_FPS> fps_samples{};
    uint32_t fps_sample_index = 0;
    uint32_t fps_sample_count = 0;
    double fps_sum = 0.0;

private:
    // Creates the entity tree of a JSON description, collects spawned entities (for on_spawned)
    ecs::Entity spawn_from_json(const Json::Value& desc, ecs::Entity parent, const SerializationContext& context,
        std::vector<ecs::Entity>& spawned);
    void apply_json(ecs::Entity e, const Json::Value& desc, const SerializationContext& context,
        std::vector<ecs::Entity>& spawned, int prefab_depth);
    // on_spawned of every component type, waits for async loads, PostLoad systems
    void finish_spawning(const std::vector<ecs::Entity>& spawned, const SerializationContext& context);
};
