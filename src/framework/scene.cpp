module;

#include <json/value.h>

module framework;

import :scene;

import std.compat;
import ecs;
import rhmath;
import name;
import glm;

namespace
{
    struct ComponentTypeTable
    {
        std::deque<ComponentType> types;  // deque: pointers stay valid
        std::unordered_map<std::string_view, ComponentType*> by_name;
        std::unordered_map<ecs::ComponentId, ComponentType*> by_id;
    };

    ComponentTypeTable& component_types()
    {
        static ComponentTypeTable table;
        return table;
    }

    // chains are short (scene objects under a prefab root); a cycle is a bug, not a hang
    constexpr int max_hierarchy_depth = 64;

    // Ancestors without a Transform (groups, e.g. an imported scene root) are identity: the local transform is
    // the world one as is (no matrix decomposition, which loses mirroring / shear)
    bool has_transformed_ancestor(const ecs::Registry& registry, ecs::Entity e)
    {
        for (int depth = 0; depth < max_hierarchy_depth; ++depth)
        {
            const ChildOf* child_of = registry.get<ChildOf>(e);
            if (!child_of)
                return false;
            e = child_of->parent;
            if (registry.has<Transform>(e))
                return true;
        }
        return false;
    }

    glm::mat4 world_matrix(const ecs::Registry& registry, ecs::Entity e)
    {
        glm::mat4 matrix(1.0f);
        for (int depth = 0; e && depth < max_hierarchy_depth; ++depth)
        {
            if (const Transform* local = registry.get<Transform>(e))
                matrix = local->matrix() * matrix;
            const ChildOf* child_of = registry.get<ChildOf>(e);
            e = child_of ? child_of->parent : ecs::null_entity;
        }
        return matrix;
    }
}

ComponentType& scene::detail::add_component_type(ComponentType type)
{
    ComponentTypeTable& table = component_types();
    if (auto it = table.by_id.find(type.id); it != table.by_id.end())
    {
        // registered again (another world): keep the first one, fill a missing on_spawned
        if (!it->second->on_spawned)
            it->second->on_spawned = std::move(type.on_spawned);
        return *it->second;
    }
    ComponentType& stored = table.types.emplace_back(std::move(type));
    table.by_name.emplace(stored.name, &stored);
    table.by_id.emplace(stored.id, &stored);
    return stored;
}

const ComponentType* scene::find_component_type(std::string_view name)
{
    const ComponentTypeTable& table = component_types();
    auto it = table.by_name.find(name);
    return it != table.by_name.end() ? it->second : nullptr;
}

const ComponentType* scene::find_component_type(ecs::ComponentId id)
{
    const ComponentTypeTable& table = component_types();
    auto it = table.by_id.find(id);
    return it != table.by_id.end() ? it->second : nullptr;
}

void scene::propagate_transforms(ecs::Registry& registry)
{
    // entities that got a Transform since the last frame
    std::vector<ecs::Entity> missing;
    ecs::Query<const Transform>(registry).each([&] (ecs::Entity e, const Transform&) {
        if (!registry.has<WorldTransform>(e))
            missing.push_back(e);
    });
    for (ecs::Entity e : missing)
        registry.add<WorldTransform>(e);

    ecs::Query<WorldTransform, const Transform>(registry).each([&] (ecs::Entity e, WorldTransform& world, const Transform& local) {
        world.value = has_transformed_ancestor(registry, e) ? Transform(world_matrix(registry, e)) : local;
    });
}

Transform scene::get_world_transform(const ecs::Registry& registry, ecs::Entity e)
{
    // from the local transforms, not WorldTransform: that one is only refreshed in the Late phase
    if (has_transformed_ancestor(registry, e))
        return Transform(world_matrix(registry, e));
    if (const Transform* local = registry.get<Transform>(e))
        return *local;
    return {};
}

void scene::set_world_transform(ecs::Registry& registry, ecs::Entity e, const Transform& world)
{
    if (!has_transformed_ancestor(registry, e))
    {
        registry.add<Transform>(e, world);
        return;
    }
    const glm::mat4 parent = world_matrix(registry, registry.get<ChildOf>(e)->parent);
    registry.add<Transform>(e, Transform(glm::inverse(parent) * world.matrix()));
}

std::string scene::get_name(const ecs::Registry& registry, ecs::Entity e)
{
    const Name* name = registry.get<Name>(e);
    return name ? name->to_string() : std::string();
}
