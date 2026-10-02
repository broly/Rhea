module;

#include <json/value.h>

module framework;

import :world;
import :scene;

import std.compat;
import assertions;
import fixed_string;

import json_utils;
import rhmath;
import :engine_clock;
import :world_script;
import dependency_collector;
import glm;
import name;
import log;

import rhobject;
import physics;
import ecs;
import paths;
import profile;

#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogWorld, Log);

namespace
{
    // prefabs referencing prefabs
    constexpr int max_prefab_depth = 16;

    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::in_set<scene::PhysicsStep>]]
    void physics_step(ecs::ResMut<phys::PhysicsScene> physics, ecs::Res<ecs::SimTime> time)
    {
        PROFILE("World::physics_step");
        physics->step((float)time->dt);
    }

    [[=ecs::system<ecs::Phase::Late>, =ecs::in_set<scene::TransformPropagation>]]
    void propagate_transforms(ecs::Registry& registry)
    {
        scene::propagate_transforms(registry);
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Transform, ChildOf)
}

World::World()
{
    // everything registered with ECS_REGISTER / SCENE_REGISTER_COMPONENTS, in any module
    scene::register_auto_components();
    ecs::add_auto_hooks(registry);
    schedule.add_auto_systems();}

void World::tick()
{
    double dt = clock->get_delta_seconds();

    if constexpr (COUNT_AVG_FPS)
    {
        if (dt > 0.0)
        {
            double fps = 1.0 / dt;

            fps_sum -= fps_samples[fps_sample_index];
            fps_samples[fps_sample_index] = fps;
            fps_sum += fps;

            fps_sample_index = (fps_sample_index + 1) % NUM_SAMPLES_AVG_FPS;

            if (fps_sample_count < NUM_SAMPLES_AVG_FPS)
                ++fps_sample_count;
        }
    }

    {
        PROFILE("World::fixed_ticks");
        schedule.run_fixed(registry, dt);
    }
    process_spawn_requests();
    {
        PROFILE("World::update");
        schedule.run_phase(registry, ecs::Phase::Update);
    }
    process_spawn_requests();

    for (auto& script : scripts)
        script->tick(dt);

    {
        PROFILE("World::late");
        schedule.run_phase(registry, ecs::Phase::Late);
    }
}

void World::init()
{
    physics = std::make_unique<phys::PhysicsScene>(phys::PhysicsSettings{
        .shape_cache_dir = paths::get_cache_path() / "physics",
    });
    registry.set_resource_ref(*physics);

    for (const ecs::Ambiguity& a : schedule.find_ambiguities())
    {
        LogWorld.Log("Systems %s and %s (%s) both access %s without an order: add ecs::before / ecs::after, "
            "or ecs::ambiguous_with if the order does not matter", a.first->name.c_str(), a.second->name.c_str(),
            std::string(ecs::phase_name(a.first->phase)).c_str(), a.conflict.c_str());
    }

    load_bootstrap_level();

    for (auto& script : scripts)
    {
        script->init(this->shared_from_this());
    }
}

bool World::load_bootstrap_level()
{
    return load_level("levels/bootstrap_level.json");
}

bool World::load_level(std::string level_path)
{
    std::optional<Json::Value> root_opt = json_utils::load_json_asset(level_path);
    if (!root_opt.has_value())
        return false;

    const Json::Value& root = root_opt.value();
    const Json::Value& entities = root["entities"];
    if (!entities.isArray())
    {
        LogWorld.Log("Level '%s': 'entities' is not an array", level_path.c_str());
        return false;
    }

    SerializationContext context{ &collector, true };
    context.current_file = level_path;

    std::vector<ecs::Entity> spawned;
    for (const Json::Value& desc : entities)
        spawn_from_json(desc, ecs::null_entity, context, spawned);

    finish_spawning(spawned, context);

    // level geometry was added one body at a time
    physics->optimize();

    LogWorld.Log("Level '%s' (%s): %zu entities", level_path.c_str(), root["name"].asString().c_str(), registry.entity_count());
    return true;
}

ecs::Entity World::spawn(Name name, const Transform& transform)
{
    const ecs::Entity e = registry.create();
    registry.add<Name>(e, name);
    registry.add<Transform>(e, transform);
    return e;
}

ecs::Entity World::spawn_prefab(std::string_view prefab_path, Name name, std::optional<Transform> transform)
{
    Json::Value desc(Json::objectValue);
    desc["prefab"] = std::string(prefab_path);
    if (!name.is_none())
        desc["name"] = name.to_string();

    SerializationContext context{ &collector, true };
    context.current_file = std::string(prefab_path);

    std::vector<ecs::Entity> spawned;
    const ecs::Entity e = spawn_from_json(desc, ecs::null_entity, context, spawned);
    if (e && transform)
        registry.add<Transform>(e, *transform);
    finish_spawning(spawned, context);
    return e;
}

ecs::Entity World::spawn_prefab_at(std::string_view prefab_path, const Transform& placement, Name name, ecs::Entity parent)
{
    Json::Value desc(Json::objectValue);
    desc["prefab"] = std::string(prefab_path);
    if (!name.is_none())
        desc["name"] = name.to_string();

    SerializationContext context{ &collector, true };
    context.current_file = std::string(prefab_path);

    std::vector<ecs::Entity> spawned;
    const ecs::Entity e = spawn_from_json(desc, parent, context, spawned);
    if (e)
    {
        // before PostLoad: static collider bodies are created where the entity is then
        Transform local = registry.has<Transform>(e) ? *registry.get<Transform>(e) : Transform{};
        const glm::quat rotation = placement.rotation.glm();
        local.position = placement.position.glm() + rotation * local.position.glm();
        local.rotation = glm::normalize(rotation * local.rotation.glm());
        registry.add<Transform>(e, local);
    }
    finish_spawning(spawned, context);
    return e;
}

ecs::Entity World::find_entity(Name name) const
{
    ecs::Entity result = ecs::null_entity;
    ecs::Query<const Name>(const_cast<ecs::Registry&>(registry)).each([&] (ecs::Entity e, const Name& entity_name) {
        if (!result && entity_name == name)
            result = e;
    });
    return result;
}

const Json::Value* World::get_prefab_json(const std::string& prefab_path)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(paths::get_assets_path() / prefab_path, error);
    if (error)
        return nullptr;
    auto it = prefab_cache.find(prefab_path);
    if (it != prefab_cache.end() && it->second.time == time)
        return &it->second.json;
    std::optional<Json::Value> json = json_utils::load_json_asset(prefab_path);
    if (!json)
        return nullptr;
    CachedPrefab& cached = prefab_cache[prefab_path];
    cached.json = std::move(*json);
    cached.time = time;
    return &cached.json;
}

void World::process_spawn_requests()
{
    PROFILE("World::process_spawn_requests");
    // copies of the requests: spawning runs PostLoad, whose systems could send more
    ecs::Events<SpawnPrefabRequest>& spawns = registry.events<SpawnPrefabRequest>();
    std::vector<SpawnPrefabRequest> requests;
    for (uint64_t id = std::max(spawn_cursor, spawns.oldest_id()); id < spawns.end_id(); ++id)
        requests.push_back(spawns.at(id));
    spawn_cursor = spawns.end_id();

    for (const SpawnPrefabRequest& request : requests)
    {
        const ecs::Entity e = spawn_prefab_at(request.prefab, request.placement, request.name, request.parent);
        if (!e)
        {
            LogWorld.Log("Spawn request: could not spawn '%s'", request.prefab.c_str());
            continue;
        }
        if (request.on_spawned)
            request.on_spawned(registry, e);
    }

    ecs::Events<DespawnRequest>& despawns = registry.events<DespawnRequest>();
    std::vector<ecs::Entity> doomed;
    for (uint64_t id = std::max(despawn_cursor, despawns.oldest_id()); id < despawns.end_id(); ++id)
        doomed.push_back(despawns.at(id).entity);
    despawn_cursor = despawns.end_id();
    for (ecs::Entity e : doomed)
        if (registry.alive(e))
            destroy_recursive(e);
}

void World::destroy_recursive(ecs::Entity e)
{
    std::vector<ecs::Entity> children;
    ecs::Query<const ChildOf>(registry).each([&] (ecs::Entity child, const ChildOf& child_of) {
        if (child_of.parent == e)
            children.push_back(child);
    });
    for (ecs::Entity child : children)
        destroy_recursive(child);
    registry.destroy(e);
}

ecs::Entity World::spawn_from_json(const Json::Value& desc, ecs::Entity parent, const SerializationContext& context,
    std::vector<ecs::Entity>& spawned)
{
    if (!desc.isObject())
        return ecs::null_entity;
    if (const Json::Value* skip = desc.find("skip"); skip && skip->asBool())
        return ecs::null_entity;

    const ecs::Entity e = registry.create();
    if (parent)
        registry.add<ChildOf>(e, parent);
    apply_json(e, desc, context, spawned, 0);
    spawned.push_back(e);
    return e;
}

void World::apply_json(ecs::Entity e, const Json::Value& desc, const SerializationContext& context,
    std::vector<ecs::Entity>& spawned, int prefab_depth)
{
    // the prefab first: this description overrides it
    if (const Json::Value* prefab = desc.find("prefab"))
    {
        checkf(prefab_depth < max_prefab_depth, "Prefab '%s': references nest too deep (a cycle?)", prefab->asString().c_str());
        if (const Json::Value* prefab_desc = get_prefab_json(prefab->asString()))
            apply_json(e, *prefab_desc, context, spawned, prefab_depth + 1);
        else
            LogWorld.Log("Prefab '%s' not found", prefab->asString().c_str());
    }

    if (const Json::Value* name = desc.find("name"))
        registry.add<Name>(e, name->asString());

    if (const Json::Value* components = desc.find("components"))
    {
        checkf(components->isObject(), "'components' of '%s' must be an object: { \"Type\": { fields } }",
            scene::get_name(registry, e).c_str());
        for (const std::string& type_name : components->getMemberNames())
        {
            const ComponentType* type = scene::find_component_type(type_name);
            if (!type || !type->load)
            {
                LogWorld.Log("Entity '%s': unknown component type '%s' (not registered with scene::register_component_type)",
                    scene::get_name(registry, e).c_str(), type_name.c_str());
                continue;
            }
            SerializationContext::__Scope scope(context, scene::get_name(registry, e) + "." + type_name);
            type->load(registry, e, (*components)[type_name], context);
        }
    }

    if (const Json::Value* children = desc.find("children"))
    {
        for (const Json::Value& child_desc : *children)
        {
            // same name as an existing child (from the prefab): override it
            ecs::Entity existing = ecs::null_entity;
            if (const Json::Value* child_name = child_desc.find("name"))
            {
                const Name name = child_name->asString();
                ecs::Query<const ChildOf, const Name>(registry).each([&] (ecs::Entity child, const ChildOf& child_of, const Name& n) {
                    if (child_of.parent == e && n == name)
                        existing = child;
                });
            }
            if (existing)
                apply_json(existing, child_desc, context, spawned, prefab_depth);
            else
                spawn_from_json(child_desc, e, context, spawned);
        }
    }
}

void World::finish_spawning(const std::vector<ecs::Entity>& spawned, const SerializationContext& context)
{
    for (ecs::Entity e : spawned)
    {
        if (!registry.alive(e))
            continue;
        // copy: on_spawned may add components (the span would move)
        const std::vector<ecs::ComponentId> ids(registry.get_component_ids(e).begin(), registry.get_component_ids(e).end());
        for (ecs::ComponentId id : ids)
        {
            const ComponentType* type = scene::find_component_type(id);
            if (type && type->on_spawned && registry.alive(e))
                type->on_spawned(*this, e, context);
        }
    }

    collector.wait();
    collector.async_load_operations.clear();

    schedule.run_phase(registry, ecs::Phase::PostLoad);
}

double World::get_time_seconds() const
{
    return clock->get_total_seconds();
}

double World::get_delta_seconds() const
{
    return clock->get_delta_seconds();
}

double World::get_avg_fps() const
{
    if (fps_sample_count == 0)
        return 0.0;

    return fps_sum / static_cast<double>(fps_sample_count);
}
