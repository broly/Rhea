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

    // Query filters: the entity must have T (not handed to the callback) / must not have T. Neither is an access
    // (the scheduler does not order systems by them): structural changes only happen at the end of a phase.
    //
    //     Query<Transform, const Health, With<Player>, Without<Dead>> q(registry);
    template<typename T>
    struct With { using type = T; };
    template<typename T>
    struct Without { using type = T; };

    namespace detail
    {
        template<typename... Ts>
        struct TypeList {};

        template<typename T> struct is_with : std::false_type {};
        template<typename T> struct is_with<With<T>> : std::true_type {};
        template<typename T> struct is_without : std::false_type {};
        template<typename T> struct is_without<Without<T>> : std::true_type {};

        template<typename L, typename T> struct append;
        template<typename... Ls, typename T> struct append<TypeList<Ls...>, T> { using type = TypeList<Ls..., T>; };

        template<typename T> struct filter_target { using type = T; };
        template<typename T> struct filter_target<With<T>> { using type = T; };
        template<typename T> struct filter_target<Without<T>> { using type = T; };

        // Ts of a Query -> the component types handed out, the With types, the Without types
        template<typename Data, typename WithL, typename WithoutL, typename... Ts>
        struct split_query
        {
            using data = Data;
            using with = WithL;
            using without = WithoutL;
        };
        template<typename Data, typename WithL, typename WithoutL, typename T, typename... Rest>
        struct split_query<Data, WithL, WithoutL, T, Rest...> : split_query<
            std::conditional_t<is_with<T>::value || is_without<T>::value, Data, typename append<Data, T>::type>,
            std::conditional_t<is_with<T>::value, typename append<WithL, typename filter_target<T>::type>::type, WithL>,
            std::conditional_t<is_without<T>::value, typename append<WithoutL, typename filter_target<T>::type>::type, WithoutL>,
            Rest...>
        {};

        template<typename Data, typename WithL, typename WithoutL>
        class QueryImpl;
    }

    // Iterates entities that have all of Ts. `T` gives mutable access, `const T` read-only; With<T> / Without<T>
    // filter without handing T out:
    //
    //     Query<Transform, const Velocity> q(registry);
    //     q.each([](Transform& t, const Velocity& v) { ... });
    //     q.each([](Entity e, Transform& t, const Velocity& v) { ... });
    //     Query<Transform, Without<Dead>> alive(registry);
    //
    // Structural changes during each() are not allowed - use Commands.
    template<typename... Ts>
    class Query : public detail::QueryImpl<
        typename detail::split_query<detail::TypeList<>, detail::TypeList<>, detail::TypeList<>, Ts...>::data,
        typename detail::split_query<detail::TypeList<>, detail::TypeList<>, detail::TypeList<>, Ts...>::with,
        typename detail::split_query<detail::TypeList<>, detail::TypeList<>, detail::TypeList<>, Ts...>::without>
    {
        using Base = detail::QueryImpl<
            typename detail::split_query<detail::TypeList<>, detail::TypeList<>, detail::TypeList<>, Ts...>::data,
            typename detail::split_query<detail::TypeList<>, detail::TypeList<>, detail::TypeList<>, Ts...>::with,
            typename detail::split_query<detail::TypeList<>, detail::TypeList<>, detail::TypeList<>, Ts...>::without>;

    public:
        using Base::Base;
    };

    namespace detail
    {
    template<typename... Ts, typename... Ws, typename... Xs>
    class QueryImpl<TypeList<Ts...>, TypeList<Ws...>, TypeList<Xs...>>
    {
        static_assert(sizeof...(Ts) > 0, "Query needs at least one component (besides With / Without)");
        static_assert((Component<std::remove_const_t<Ts>> && ...),
            "Query<T> (write) / Query<const T> (read): component types, not references");
        static_assert((Component<Ws> && ...) && (Component<Xs> && ...), "With<T> / Without<T>: T is a component type");

    public:
        explicit QueryImpl(Registry& in_registry)
            : registry(&in_registry)
        {}

        template<typename F>
        void each(F&& f) const
        {
            const std::array<ComponentId, sizeof...(Ts)> ids = { component_id<std::remove_const_t<Ts>>()... };
            const std::vector<Archetype*>& archetypes = registry->match(required_ids(ids));

            Registry::IterationScope scope(*registry);
            [&]<size_t... I>(std::index_sequence<I...>)
            {
                for (Archetype* archetype : archetypes)
                {
                    const size_t count = archetype->size();
                    if (count == 0 || excluded(*archetype))
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
            for (Archetype* archetype : registry->match(required_ids(ids)))
                if (!excluded(*archetype))
                    result += archetype->size();
            return result;
        }

        // the entity passes the With / Without filters (and has the component types)
        bool matches(Entity e) const
        {
            if (!registry->alive(e))
                return false;
            return (registry->has<std::remove_const_t<Ts>>(e) && ...)
                && (registry->has<Ws>(e) && ...)
                && (!registry->has<Xs>(e) && ...);
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
        // the component types and the With types, sorted, unique
        static std::vector<ComponentId> required_ids(const std::array<ComponentId, sizeof...(Ts)>& ids)
        {
            std::vector<ComponentId> result(ids.begin(), ids.end());
            (result.push_back(component_id<Ws>()), ...);
            std::ranges::sort(result);
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }

        static bool excluded(const Archetype& archetype)
        {
            if constexpr (sizeof...(Xs) == 0)
                return false;
            else
                return (std::ranges::contains(archetype.get_types(), component_id<Xs>()) || ...);
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
}
