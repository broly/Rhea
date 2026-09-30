module;

#include <json/value.h>

module trees;

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
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogTrees, Log);

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

    [[=scene::on_spawned<Trees>]]
    void spawn_trees(World& world, ecs::Entity root, const SerializationContext& context)
    {
        ecs::Registry& registry = world.registry;
        const Trees trees = *registry.get<Trees>(root);
        const std::string root_name = scene::get_name(registry, root);

        const std::optional<TreePlacement> placement = load_asset<TreePlacement>(trees.placement, context);
        if (!placement)
        {
            LogTrees.Log<Error>("Trees '%s': placement '%s' not found", root_name.c_str(), trees.placement.c_str());
            return;
        }

        const TerrainData* terrain = trees.snap_to_terrain ? find_terrain(registry) : nullptr;
        if (trees.snap_to_terrain && !terrain)
            LogTrees.Log<Warning>("Trees '%s': no terrain to stand on (it must be spawned first), heights come from the placement",
                root_name.c_str());

        const Transform root_transform = scene::get_world_transform(registry, root);
        phys::PhysicsScene& physics = world.get_physics();

        // nullopt: failed to load, reported once
        std::map<std::string, std::optional<TreeSpecies>> species_by_path;
        uint32_t count = 0;
        for (const TreeInstance& instance : placement->instances)
        {
            auto found = species_by_path.find(instance.species);
            if (found == species_by_path.end())
            {
                found = species_by_path.emplace(instance.species, load_asset<TreeSpecies>(instance.species, context)).first;
                if (!found->second || !found->second->renderer.mesh.is_valid())
                {
                    LogTrees.Log<Error>("Trees '%s': species '%s' not found", root_name.c_str(), instance.species.c_str());
                    found->second.reset();
                }
            }
            if (!found->second)
                continue;
            const TreeSpecies& species = *found->second;

            Transform local;
            local.position = instance.position;
            local.rotation = glm::angleAxis(glm::radians(instance.yaw), glm::vec3(0.0f, 1.0f, 0.0f));
            local.scale = glm::vec3(instance.scale);
            if (terrain)
            {
                const glm::vec3 world_position = root_transform.position.glm() + instance.position.glm();
                if (const std::optional<float> height = terrain::height_at(*terrain, glm::vec2(world_position.x, world_position.z)))
                    local.position.y = *height - trees.sink * instance.scale - root_transform.position.y;
            }

            const ecs::Entity tree = registry.create();
            registry.add<Name>(tree, std::format("{}_{}", root_name, count));
            registry.add<Transform>(tree, local);
            registry.add<ChildOf>(tree, root);
            registry.add<MeshRenderer>(tree, species.renderer);

            if (species.trunk_radius > 0.0f && species.trunk_height > 0.0f)
            {
                // a capsule along y, centered on its body
                const float radius = species.trunk_radius * instance.scale;
                const float half_height = std::max(0.5f * species.trunk_height * instance.scale - radius, 0.05f);
                const glm::quat rotation = local.rotation;
                const glm::vec3 base = root_transform.position.glm() + local.position.glm()
                    + rotation * (species.trunk_offset.glm() * instance.scale);
                TreeCollider collider;
                collider.body = physics.create_body({
                    .shape = physics.create_shape(phys::CapsuleShape{ .half_height = half_height, .radius = radius }),
                    .position = base + glm::vec3(0.0f, half_height + radius, 0.0f),
                    .motion = phys::Motion::fixed,
                    .category = phys::Category::static_world,
                    .user_data = tree.bits(),
                });
                registry.add<TreeCollider>(tree, collider);
            }
            ++count;
        }

        registry.get<Trees>(root)->count = count;
        LogTrees.Log("Trees '%s': %u trees of %zu species (%s)", root_name.c_str(), count, species_by_path.size(),
            trees.placement.c_str());
    }

    [[=ecs::on_remove]]
    void destroy_tree_collider(ecs::Registry& registry, ecs::Entity, TreeCollider& collider)
    {
        phys::PhysicsScene* physics = registry.find_resource<phys::PhysicsScene>();
        if (collider.body.is_valid() && physics)
            physics->destroy_body(collider.body);
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Trees, TreeCollider)
}
