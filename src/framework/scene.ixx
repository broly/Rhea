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

        // Component types that register themselves (scene_macros.h), no central list:
        //
        //     struct [[=scene::runtime_only]] CharacterInput { ... };      // .ixx: inspector only, not in JSON
        //
        //     namespace                                                     // .cpp of the module
        //     {
        //         [[=scene::on_spawned<GltfScene>]]
        //         void import_scene(World& world, ecs::Entity e, const SerializationContext& context) { ... }
        //
        //         SCENE_REGISTER_COMPONENTS(GltfScene, CharacterInput)     // after the on_spawned functions
        //     }
        //
        // Static initialization only records them; register_auto_components (World) does the registration,
        // which needs Name, reflection tables, ... - initialized in any order before main.
        struct RuntimeOnly {};
        inline constexpr RuntimeOnly runtime_only;

        template<ecs::Component T>
        struct OnSpawned {};
        template<ecs::Component T>
        inline constexpr OnSpawned<T> on_spawned;

        void register_auto_components();

        namespace detail
        {
            void register_auto_component(void (*registration)());

            template<ecs::Component T>
            void register_annotated_component_type(
                std::function<void(World&, ecs::Entity, const SerializationContext&)> on_spawned = {})
            {
                register_component_type<T, !reflect::has_annotation<RuntimeOnly>(^^T)>(std::move(on_spawned));
            }

            template<ecs::Component T>
            void register_annotated_component_type_thunk()
            {
                register_annotated_component_type<T>();
            }

            struct OnSpawnedDecl
            {
                std::meta::info function;
                std::meta::info component;
            };

            // Functions of a namespace annotated with scene::on_spawned<T>
            consteval std::vector<OnSpawnedDecl> annotated_on_spawned(std::meta::info ns)
            {
                std::vector<OnSpawnedDecl> result;
                for (std::meta::info member : std::meta::members_of(ns, std::meta::access_context::unchecked()))
                {
                    if (!std::meta::is_function(member))
                        continue;
                    for (std::meta::info annotation : std::meta::annotations_of(member))
                    {
                        const std::meta::info type = ecs::detail::annotation_type(annotation);
                        if (ecs::detail::is_annotation_of(type, ^^OnSpawned))
                            result.push_back({ member, std::meta::template_arguments_of(type)[0] });
                    }
                }
                return result;
            }

            // Keyed by the marker type of SCENE_REGISTER_COMPONENTS, not by std::meta::info (see ecs::detail::SystemsOf)
            template<typename Marker>
            struct OnSpawnedOf
            {
                static constexpr std::span<const OnSpawnedDecl> decls =
                    std::define_static_array(annotated_on_spawned(std::meta::parent_of(^^Marker)));
            };

            template<typename Marker, size_t I>
            struct OnSpawnedAt
            {
                static constexpr OnSpawnedDecl decl = OnSpawnedOf<Marker>::decls[I];
                static constexpr void (*function)(World&, ecs::Entity, const SerializationContext&) = &[:decl.function:];
                using Component = [:decl.component:];

                // registration is idempotent: fills on_spawned of a type listed in another file
                static void add() { register_annotated_component_type<Component>(function); }
            };

            template<typename Marker, size_t... I>
            void register_on_spawned(std::index_sequence<I...>)
            {
                (register_auto_component(&OnSpawnedAt<Marker, I>::add), ...);
            }

            template<typename Marker, ecs::Component... Ts>
            bool register_components()
            {
                (register_auto_component(&register_annotated_component_type_thunk<Ts>), ...);
                register_on_spawned<Marker>(std::make_index_sequence<OnSpawnedOf<Marker>::decls.size()>{});
                return true;
            }
        }

        const ComponentType* find_component_type(std::string_view name);
        const ComponentType* find_component_type(ecs::ComponentId id);

        // Root to child order is not needed: each entity composes its parent chain
        void propagate_transforms(ecs::Registry& registry);

        // System set of the Late system that computes WorldTransform: systems reading it run
        // [[=ecs::after<scene::TransformPropagation>]]
        struct TransformPropagation {};

        // System set of the FixedPost system that steps the physics: queries of the tick's state (weapons, damage)
        // run [[=ecs::before<scene::PhysicsStep>]]
        struct PhysicsStep {};

        // World transform composed from the local transforms right now (WorldTransform lags until the Late phase)
        Transform get_world_transform(const ecs::Registry& registry, ecs::Entity e);

        // Sets the local Transform so that the entity ends up at `world` (parent chain taken into account)
        void set_world_transform(ecs::Registry& registry, ecs::Entity e, const Transform& world);

        // Name of the entity, "" if unnamed
        std::string get_name(const ecs::Registry& registry, ecs::Entity e);
    }
}
