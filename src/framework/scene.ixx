module;

#include <json/value.h>

export module framework:scene;

import std.compat;
import ecs;
import rhmath;
import name;
import rhobject;
import reflect;
import properties;

import :core;

// Scene layer over the ECS: what every world entity may have, and the table of component types that
// can be written in prefab / level JSON and edited in the inspector.
//
// An entity of the scene usually has:
//     Name            - for lookups, debug UI, cache keys (the `name` of its JSON)
//     Transform       - local: relative to the parent's WorldTransform, or to the world without ChildOf
//     ChildOf         - optional parent
//     WorldTransform  - computed every frame (Late phase) - what rendering and debug tools read
export
{
    struct ChildOf
    {
        ecs::Entity parent;
    };

    struct WorldTransform
    {
        Transform value;
    };

    // A component type known by its name, e.g. "Light" in
    //     "components": { "Transform": {...}, "Light": { "intensity": 2.0 } }
    struct ComponentType
    {
        std::string_view name;
        ecs::ComponentId id = 0;

        // Reads JSON fields over the entity's component (adds a default one first when missing),
        // so JSON applied later (level over prefab) overrides only the fields it names. nullptr: not in JSON.
        void (*load)(ecs::Registry& registry, ecs::Entity e, const Json::Value& fields, const SerializationContext& context) = nullptr;

        // Fields for the inspector (empty for tags)
        reflect::PropertyObject (*properties)(void* component) = nullptr;

        // Called once the entity is spawned from JSON with all its components (resolve assets, spawn children...)
        std::function<void(World& world, ecs::Entity e, const SerializationContext& context)> on_spawned;
    };

    namespace scene
    {
        namespace detail
        {
            ComponentType& add_component_type(ComponentType type);
        }

        // Makes T loadable from JSON by its type name and editable in the inspector. Idempotent.
        // Loadable = false: runtime state (fields without JSON serialization), inspector only.
        template<ecs::Component T, bool Loadable = true>
        const ComponentType& register_component_type(
            std::function<void(World&, ecs::Entity, const SerializationContext&)> on_spawned = {})
        {
            ComponentType type;
            type.name = ecs::component_info(ecs::component_id<T>()).name;
            type.id = ecs::component_id<T>();
            if constexpr (Loadable)
            {
                type.load = [] (ecs::Registry& registry, ecs::Entity e, const Json::Value& fields, const SerializationContext& context)
                {
                    T value = registry.has<T>(e) ? *registry.get<T>(e) : T{};
                    if constexpr (!std::is_empty_v<T>)
                        reflect::json::visit_serialize(fields, value, context);
                    registry.add<T>(e, std::move(value));
                };
            }
            type.properties = [] (void* component) -> reflect::PropertyObject
            {
                // [[=rh::edit]] fields, or all public fields of a plain data component (e.g. Transform)
                if constexpr (std::is_empty_v<T>)
                    return {};
                else
                    return reflect::PropertyObject{
                        .object = component,
                        .properties = &reflect::nested_properties_of<T>(),
                        .type_name = reflect::type_name_of<T>(),
                    };
            };
            type.on_spawned = std::move(on_spawned);
            return detail::add_component_type(std::move(type));
        }

        const ComponentType* find_component_type(std::string_view name);
        const ComponentType* find_component_type(ecs::ComponentId id);

        // Root to child order is not needed: each entity composes its parent chain
        void propagate_transforms(ecs::Registry& registry);

        // World transform composed from the local transforms right now (WorldTransform lags until the Late phase)
        Transform get_world_transform(const ecs::Registry& registry, ecs::Entity e);

        // Sets the local Transform so that the entity ends up at `world` (parent chain taken into account)
        void set_world_transform(ecs::Registry& registry, ecs::Entity e, const Transform& world);

        // Name of the entity, "" if unnamed
        std::string get_name(const ecs::Registry& registry, ecs::Entity e);
    }
}
