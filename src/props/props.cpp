module;

#include <json/value.h>

module props;

import std.compat;
import fixed_string;
import glm;
import rhmath;
import reflect;
import rhobject;
import name;
import ecs;
import framework;
import assets;
import physics;
import rhcomponents;
import terrain;
import json_utils;
import paths;
import log;

#include "logging/log_macro.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogProps, Log);

namespace
{
    template<typename T>
    std::optional<T> load_asset(const std::string& path, const SerializationContext& context)
    {
        if (!std::filesystem::exists(paths::get_assets_path() / path))
            return std::nullopt;
        const std::optional<Json::Value> json = json_utils::load_json_asset(path);
        if (!json)
            return std::nullopt;
        T value;
        reflect::json::visit_serialize(*json, value, context);
        return value;
    }

    // The terrain of the level, if it is spawned already
    const TerrainData* find_terrain(ecs::Registry& registry)
    {
        const TerrainData* result = nullptr;
        ecs::Query<const Terrain>(registry).each([&] (const Terrain& terrain) {
            if (terrain.data)
                result = terrain.data.get();
        });
        return result;
    }

    struct LoadedProp
    {
        Prop prop;
        // at scale 1; invalid: no collision
        phys::Shape shape;
    };

    std::optional<LoadedProp> load_prop(const std::string& path, phys::PhysicsScene& physics, const SerializationContext& context)
    {
        std::optional<Prop> prop = load_asset<Prop>(path, context);
        if (!prop || !prop->renderer.mesh.is_valid())
            return std::nullopt;

        LoadedProp loaded{ .prop = std::move(*prop) };
        if (loaded.prop.collision != MeshCollision::none)
        {
            const MeshHandle mesh = loaded.prop.collision_mesh.is_valid() ? loaded.prop.collision_mesh : loaded.prop.renderer.mesh;
            // the mesh is a LOD made for this: nothing left to simplify
            const MeshCollision type = loaded.prop.collision == MeshCollision::simplified ? MeshCollision::triangles : loaded.prop.collision;
            loaded.shape = cook_mesh_collision(physics, mesh.get(), glm::vec3(1.0f), { .type = type }, path);
        }
        return loaded;
    }

    [[=scene::on_spawned<Props>]]
    void spawn_props(World& world, ecs::Entity root, const SerializationContext& context)
    {
        ecs::Registry& registry = world.registry;
        const Props props = *registry.get<Props>(root);
        const std::string root_name = scene::get_name(registry, root);

        const std::optional<PropPlacement> placement = load_asset<PropPlacement>(props.placement, context);
        if (!placement)
        {
            LogProps.Log<Error>("Props '%s': placement '%s' not found", root_name.c_str(), props.placement.c_str());
            return;
        }

        const TerrainData* terrain = props.snap_to_terrain ? find_terrain(registry) : nullptr;
        if (props.snap_to_terrain && !terrain)
            LogProps.Log<Warning>("Props '%s': no terrain to lie on (it must be spawned first), heights come from the placement",
                root_name.c_str());

        const Transform root_transform = scene::get_world_transform(registry, root);
        phys::PhysicsScene& physics = world.get_physics();

        // nullopt: failed to load, reported once
        std::map<std::string, std::optional<LoadedProp>> prop_by_path;
        uint32_t count = 0;
        for (const PropInstance& instance : placement->instances)
        {
            auto found = prop_by_path.find(instance.prop);
            if (found == prop_by_path.end())
            {
                found = prop_by_path.emplace(instance.prop, load_prop(instance.prop, physics, context)).first;
                if (!found->second)
                    LogProps.Log<Error>("Props '%s': prop '%s' not found", root_name.c_str(), instance.prop.c_str());
            }
            if (!found->second)
                continue;
            const LoadedProp& loaded = *found->second;

            Transform local;
            local.position = instance.position;
            local.rotation = glm::normalize(instance.rotation.glm());
            local.scale = glm::vec3(instance.scale);
            if (terrain)
            {
                const glm::vec3 world_position = root_transform.position.glm() + instance.position.glm();
                if (const std::optional<float> height = terrain::height_at(*terrain, glm::vec2(world_position.x, world_position.z)))
                    local.position.y = *height - instance.sink - root_transform.position.y;
            }

            const ecs::Entity entity = registry.create();
            registry.add<Name>(entity, std::format("{}_{}", root_name, count));
            registry.add<Transform>(entity, local);
            registry.add<ChildOf>(entity, root);
            registry.add<MeshRenderer>(entity, loaded.prop.renderer);

            if (loaded.shape)
            {
                // the body is created by the MeshCollider systems, at the entity's world transform
                MeshCollider collider{ .type = loaded.prop.collision };
                collider.shape = physics.create_scaled_shape(loaded.shape, glm::vec3(instance.scale));
                registry.add<MeshCollider>(entity, std::move(collider));
            }
            ++count;
        }

        registry.get<Props>(root)->count = count;
        LogProps.Log("Props '%s': %u props of %zu kinds (%s)", root_name.c_str(), count, prop_by_path.size(),
            props.placement.c_str());
    }

    SCENE_REGISTER_COMPONENTS(Props)
}
