export module ecs:component;

import std.compat;

export namespace ecs
{
    // Runtime id of a component type, dense (0, 1, 2...) in order of first use.
    // Not stable between runs - never serialize it, use ComponentInfo::name.
    using ComponentId = uint32_t;

    // Type-erased operations on a component type, enough to store it in archetype columns
    struct ComponentInfo
    {
        ComponentId id = 0;
        std::string_view name;
        size_t size = 0;    // 0 for tags (empty types): they have no storage
        size_t align = 1;
        void (*relocate)(void* dst, void* src) = nullptr;  // move-constructs dst from src, destroys src
        void (*destroy)(void* p) = nullptr;
    };

    // Any movable non-reference, non-cv type can be a component. Empty types are tags.
    template<typename T>
    concept Component = std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
        && !std::is_array_v<T> && std::is_move_constructible_v<T> && std::is_destructible_v<T>;

    namespace detail
    {
        ComponentId register_component(ComponentInfo info);

        template<typename T>
        consteval std::string_view type_name()
        {
            return std::define_static_string(std::meta::display_string_of(std::meta::dealias(^^T)));
        }

        template<Component T>
        ComponentInfo make_component_info()
        {
            ComponentInfo info;
            info.name = type_name<T>();
            if constexpr (!std::is_empty_v<T>)
            {
                info.size = sizeof(T);
                info.align = alignof(T);
                info.relocate = [](void* dst, void* src)
                {
                    T* from = static_cast<T*>(src);
                    std::construct_at(static_cast<T*>(dst), std::move(*from));
                    std::destroy_at(from);
                };
                info.destroy = [](void* p) { std::destroy_at(static_cast<T*>(p)); };
            }
            return info;
        }

        // Shared instance handed out for tag components, which are not stored
        template<Component T>
        T& tag_instance()
        {
            static T instance{};
            return instance;
        }
    }

    template<Component T>
    ComponentId component_id()
    {
        static const ComponentId id = detail::register_component(detail::make_component_info<T>());
        return id;
    }

    const ComponentInfo& component_info(ComponentId id);
    size_t registered_component_count();
}
