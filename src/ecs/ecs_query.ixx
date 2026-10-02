export module ecs:query;

import std.compat;

import :entity;
import :component;
import :registry;

export namespace ecs
{
    // What a system reads / writes; the scheduler uses it to order systems and (later) run them in parallel
    struct Access
    {
        std::vector<ComponentId> reads;
        std::vector<ComponentId> writes;
        struct Resource
        {
            const void* key;
            std::string_view name;
        };
        std::vector<Resource> resource_reads;
        std::vector<Resource> resource_writes;
        // events sent (EventWriter): conflict with readers of the queue, not with other writers (the events of
        // two unordered writers come in schedule order, which is deterministic)
        std::vector<Resource> resource_appends;
        bool exclusive = false;  // takes Registry& - conflicts with everything
    };

    // Iterates entities that have all of Ts. `T` gives mutable access, `const T` read-only:
    //
    //     Query<Transform, const Velocity> q(registry);
    //     q.each([](Transform& t, const Velocity& v) { ... });
    //     q.each([](Entity e, Transform& t, const Velocity& v) { ... });
    //
    // Structural changes during each() are not allowed - use Commands.
    template<typename... Ts>
    class Query
    {
        static_assert(sizeof...(Ts) > 0, "Query needs at least one component");
        static_assert((Component<std::remove_const_t<Ts>> && ...),
            "Query<T> (write) / Query<const T> (read): component types, not references");

    public:
        explicit Query(Registry& in_registry)
            : registry(&in_registry)
        {}

        template<typename F>
        void each(F&& f) const
        {
            const std::array<ComponentId, sizeof...(Ts)> ids = { component_id<std::remove_const_t<Ts>>()... };
            const std::vector<Archetype*>& archetypes = registry->match(sorted_ids(ids));

            Registry::IterationScope scope(*registry);
            [&]<size_t... I>(std::index_sequence<I...>)
            {
                for (Archetype* archetype : archetypes)
                {
                    const size_t count = archetype->size();
                    if (count == 0)
                        continue;

                    const std::tuple<Ts*...> bases = { column_base<Ts>(*archetype, ids[I])... };
                    const Entity* entities = archetype->get_entities().data();
                    for (size_t row = 0; row < count; ++row)
                    {
                        if constexpr (std::is_invocable_v<F&, Entity, Ts&...>)
                            f(entities[row], element<Ts>(std::get<I>(bases), row)...);
                        else
                            f(element<Ts>(std::get<I>(bases), row)...);
                    }
                }
            }(std::index_sequence_for<Ts...>{});
        }

        size_t count() const
        {
            const std::array<ComponentId, sizeof...(Ts)> ids = { component_id<std::remove_const_t<Ts>>()... };
            size_t result = 0;
            for (Archetype* archetype : registry->match(sorted_ids(ids)))
                result += archetype->size();
            return result;
        }

        bool empty() const { return count() == 0; }

        // Component of any entity, not only the iterated one (nullptr if it has none): T is one of Ts,
        // the result is const if the query reads it. With one component type T can be omitted.
        template<typename T = std::remove_const_t<std::tuple_element_t<0, std::tuple<Ts...>>>>
            requires (std::is_same_v<T, std::remove_const_t<Ts>> || ...)
        auto* get(Entity e) const
        {
            if constexpr ((std::is_same_v<T, Ts> || ...))
                return registry->get<T>(e);
            else
                return static_cast<const T*>(registry->get<T>(e));
        }

        static void describe_access(Access& access)
        {
            (describe_one<Ts>(access), ...);
        }

    private:
        static std::vector<ComponentId> sorted_ids(const std::array<ComponentId, sizeof...(Ts)>& ids)
        {
            std::vector<ComponentId> result(ids.begin(), ids.end());
            std::ranges::sort(result);
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }

        template<typename T>
        static void describe_one(Access& access)
        {
            const ComponentId id = component_id<std::remove_const_t<T>>();
            if constexpr (std::is_const_v<T>)
                access.reads.push_back(id);
            else
                access.writes.push_back(id);
        }

        template<typename T>
        static T* column_base(const Archetype& archetype, ComponentId id)
        {
            if constexpr (std::is_empty_v<std::remove_const_t<T>>)
                return &detail::tag_instance<std::remove_const_t<T>>();
            else
                return reinterpret_cast<T*>(archetype.get_columns()[archetype.find_column(id)].data);
        }

        template<typename T>
        static T& element(T* base, size_t row)
        {
            if constexpr (std::is_empty_v<std::remove_const_t<T>>)
                return *base;
            else
                return base[row];
        }

        Registry* registry;
    };
}
