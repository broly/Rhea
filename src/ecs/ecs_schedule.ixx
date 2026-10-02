export module ecs:schedule;

import std.compat;

import :entity;
import :component;
import :registry;
import :query;
import :commands;
import :events;

export namespace ecs
{
    // Order of a frame: [FixedPre, Fixed, FixedPost] x (0..N fixed ticks), then Update, Late.
    enum class Phase : uint8_t
    {
        FixedPre,   // fixed tick: input commands, network receive
        Fixed,      // fixed tick: simulation - movement, gameplay (deterministic, replicated state)
        FixedPost,  // fixed tick: physics step, network send
        Update,     // once per frame, variable dt: animation, cosmetics
        Late,       // once per frame after Update: transform propagation, render sync
        PostLoad,   // not per frame: after a level / prefab batch is loaded (physics bodies, caches)
        Count
    };

    std::string_view phase_name(Phase phase);

    // Resource: fixed-step simulation time. `tick` is the tick being simulated.
    struct SimTime
    {
        uint64_t tick = 0;
        double dt = 1.0 / 60.0;
        double time = 0.0;
    };

    // Resource: real frame time for Update / Late.
    // alpha in [0, 1) - how far real time is past the last simulated tick (for render interpolation).
    struct FrameTime
    {
        uint64_t frame = 0;
        double dt = 0.0;
        double time = 0.0;
        double alpha = 0.0;
    };

    // System parameter: read-only resource
    template<typename T>
    class Res
    {
    public:
        explicit Res(const T& r) : ref(&r) {}
        const T& operator*() const { return *ref; }
        const T* operator->() const { return ref; }

    private:
        const T* ref;
    };

    // System parameter: mutable resource
    template<typename T>
    class ResMut
    {
    public:
        explicit ResMut(T& r) : ref(&r) {}
        T& operator*() const { return *ref; }
        T* operator->() const { return ref; }

    private:
        T* ref;
    };

    // System parameters: a resource that may be missing (nullptr until something creates it). Same access as
    // Res / ResMut; for features whose resource only exists on some levels (the navigation mesh).
    template<typename T>
    class OptRes
    {
    public:
        explicit OptRes(const T* r) : ref(r) {}
        explicit operator bool() const { return ref != nullptr; }
        const T* get() const { return ref; }
        const T& operator*() const { return *ref; }
        const T* operator->() const { return ref; }

    private:
        const T* ref;
    };

    template<typename T>
    class OptResMut
    {
    public:
        explicit OptResMut(T* r) : ref(r) {}
        explicit operator bool() const { return ref != nullptr; }
        T* get() const { return ref; }
        T& operator*() const { return *ref; }
        T* operator->() const { return ref; }

    private:
        T* ref;
    };

    // System parameter: sends events of type T (Registry::events<T>). Sent at once, not at the end of the phase:
    // readers later in the phase see them.
    template<typename T>
    class EventWriter
    {
    public:
        explicit EventWriter(Events<T>& e) : events(&e) {}
        void send(T event) const { events->send(std::move(event)); }
        template<typename... Args>
        void emplace(Args&&... args) const { events->emplace(std::forward<Args>(args)...); }

    private:
        Events<T>* events;
    };

    // System parameter: the events of type T this system has not read yet (its own cursor), oldest first:
    //
    //     void on_damage(EventReader<DamageEvent> damage) { for (const DamageEvent& d : damage.read()) ... }
    //
    // read() marks them read; events the system does not read in a run are kept for its next run while they
    // live (see Events).
    template<typename T>
    class EventReader
    {
    public:
        class Range
        {
        public:
            class iterator
            {
            public:
                using value_type = T;
                using difference_type = std::ptrdiff_t;
                iterator() = default;
                iterator(const Events<T>* e, uint64_t i) : events(e), id(i) {}
                const T& operator*() const { return events->at(id); }
                const T* operator->() const { return &events->at(id); }
                iterator& operator++() { ++id; return *this; }
                iterator operator++(int) { iterator old = *this; ++id; return old; }
                bool operator==(const iterator& other) const { return id == other.id; }

            private:
                const Events<T>* events = nullptr;
                uint64_t id = 0;
            };

            Range(const Events<T>* e, uint64_t b, uint64_t en) : events(e), first(b), last(en) {}
            iterator begin() const { return { events, first }; }
            iterator end() const { return { events, last }; }
            size_t size() const { return size_t(last - first); }
            bool empty() const { return first == last; }

        private:
            const Events<T>* events;
            uint64_t first;
            uint64_t last;
        };

        EventReader(const Events<T>& e, uint64_t& c) : events(&e), cursor(&c) {}

        // the unread events; marks them read
        Range read() const
        {
            const uint64_t first = std::max(*cursor, events->oldest_id());
            *cursor = events->end_id();
            return Range(events, first, *cursor);
        }
        size_t unread() const { return size_t(events->end_id() - std::max(*cursor, events->oldest_id())); }
        bool empty() const { return unread() == 0; }
        // marks every event read without looking at them
        void clear() const { *cursor = events->end_id(); }

    private:
        const Events<T>* events;
        uint64_t* cursor;
    };

    struct SystemContext
    {
        Registry& registry;
        Commands& commands;
    };

    // How a system parameter type is fetched and what it accesses. Specialize to add parameter kinds.
    // A specialization with a `State` type gets one State per system, kept between runs:
    // `static P fetch(SystemContext&, State&)` instead of `fetch(SystemContext&)`.
    template<typename P>
    struct SystemParam
    {
        static_assert(false, "unsupported system parameter: use Query<...>, Commands&, Res<T>, ResMut<T>, OptRes<T>, OptResMut<T>, "
            "EventReader<T>, EventWriter<T> or Registry&");
    };

    template<typename... Ts>
    struct SystemParam<Query<Ts...>>
    {
        static Query<Ts...> fetch(SystemContext& c) { return Query<Ts...>(c.registry); }
        static void access(Access& a) { Query<Ts...>::describe_access(a); }
    };

    template<>
    struct SystemParam<Commands>
    {
        static Commands& fetch(SystemContext& c) { return c.commands; }
        static void access(Access&) {}
    };

    // Direct registry access: structural changes allowed, runs alone
    template<>
    struct SystemParam<Registry>
    {
        static Registry& fetch(SystemContext& c) { return c.registry; }
        static void access(Access& a) { a.exclusive = true; }
    };

    template<typename T>
    struct SystemParam<Res<T>>
    {
        static Res<T> fetch(SystemContext& c) { return Res<T>(c.registry.resource<T>()); }
        static void access(Access& a) { a.resource_reads.push_back({ Registry::resource_key<T>(), detail::type_name<T>() }); }
    };

    template<typename T>
    struct SystemParam<ResMut<T>>
    {
        static ResMut<T> fetch(SystemContext& c) { return ResMut<T>(c.registry.resource<T>()); }
        static void access(Access& a) { a.resource_writes.push_back({ Registry::resource_key<T>(), detail::type_name<T>() }); }
    };

    template<typename T>
    struct SystemParam<OptRes<T>>
    {
        static OptRes<T> fetch(SystemContext& c) { return OptRes<T>(c.registry.find_resource<T>()); }
        static void access(Access& a) { a.resource_reads.push_back({ Registry::resource_key<T>(), detail::type_name<T>() }); }
    };

    template<typename T>
    struct SystemParam<OptResMut<T>>
    {
        static OptResMut<T> fetch(SystemContext& c) { return OptResMut<T>(c.registry.find_resource<T>()); }
        static void access(Access& a) { a.resource_writes.push_back({ Registry::resource_key<T>(), detail::type_name<T>() }); }
    };

    template<typename T>
    struct SystemParam<EventWriter<T>>
    {
        static EventWriter<T> fetch(SystemContext& c) { return EventWriter<T>(c.registry.events<T>()); }
        static void access(Access& a)
        {
            a.resource_appends.push_back({ Registry::resource_key<Events<T>>(), detail::type_name<Events<T>>() });
        }
    };

    template<typename T>
    struct SystemParam<EventReader<T>>
    {
        struct State
        {
            // the next event to read; on the first run the oldest alive one
            uint64_t cursor = 0;
            bool started = false;
        };
        static EventReader<T> fetch(SystemContext& c, State& state)
        {
            Events<T>& events = c.registry.events<T>();
            if (!state.started)
            {
                state.cursor = events.oldest_id();
                state.started = true;
            }
            return EventReader<T>(events, state.cursor);
        }
        static void access(Access& a)
        {
            a.resource_reads.push_back({ Registry::resource_key<Events<T>>(), detail::type_name<Events<T>>() });
        }
    };

    namespace detail
    {
        template<typename P>
        concept StatefulParam = requires { typename SystemParam<P>::State; };

        template<typename P>
        struct param_state { using type = std::monostate; };
        template<StatefulParam P>
        struct param_state<P> { using type = typename SystemParam<P>::State; };

        template<typename P>
        decltype(auto) fetch_param(SystemContext& c, typename param_state<P>::type& state)
        {
            if constexpr (StatefulParam<P>)
                return SystemParam<P>::fetch(c, state);
            else
                return SystemParam<P>::fetch(c);
        }
    }

    namespace detail
    {
        template<typename F>
        struct callable_params : callable_params<decltype(&F::operator())> {};

        template<typename R, typename... P>
        struct callable_params<R(*)(P...)> { using type = std::tuple<P...>; };

        template<typename R, typename... P>
        struct callable_params<R(P...)> { using type = std::tuple<P...>; };

        template<typename C, typename R, typename... P>
        struct callable_params<R(C::*)(P...)> { using type = std::tuple<P...>; };

        template<typename C, typename R, typename... P>
        struct callable_params<R(C::*)(P...) const> { using type = std::tuple<P...>; };
    }

    namespace detail
    {
        // Qualified name of a type, e.g. "scene::TransformPropagation"
        consteval std::string qualified_name(std::meta::info type)
        {
            type = std::meta::dealias(type);
            std::string name(std::meta::display_string_of(type));
            for (std::meta::info scope = std::meta::parent_of(type); scope != ^^::; scope = std::meta::parent_of(scope))
            {
                if (std::meta::has_identifier(scope))
                    name = std::string(std::meta::identifier_of(scope)) + "::" + name;
            }
            return name;
        }
    }

    // Name of a system set: any type used as a label, e.g. `struct TransformPropagation {};`
    template<typename Set>
    consteval std::string_view set_name()
    {
        return std::define_static_string(detail::qualified_name(^^Set));
    }

    struct System
    {
        std::string name;
        Phase phase = Phase::Update;
        Access access;
        // Order inside the phase, by set names (set_name<T>()): the system runs after every system of
        // the phase that is in one of its `after` sets, and before every one in its `before` sets
        std::vector<std::string_view> sets;
        std::vector<std::string_view> before;
        std::vector<std::string_view> after;
        // Unordered access conflicts with systems of these sets are intended: not reported as ambiguities
        std::vector<std::string_view> ambiguous_with;
        std::move_only_function<void(SystemContext&)> run;
        double last_ms = 0.0;

        bool in_set(std::string_view set) const { return std::ranges::contains(sets, set); }
    };

    // Two systems of a phase that access the same data (at least one writes) with no order between them:
    // the result depends on an order nobody chose
    struct Ambiguity
    {
        const System* first = nullptr;
        const System* second = nullptr;
        std::string conflict;   // what both access
    };

    // Systems are plain functions (or non-generic lambdas) whose parameters say what they need:
    //
    //     void apply_velocity(Query<Transform, const Velocity> q, Res<SimTime> time) { ... }
    //     schedule.add<&apply_velocity>(Phase::Fixed);
    //     schedule.add(Phase::Update, "spin", [](Query<Transform> q) { ... }).after.push_back(set_name<Physics>());
    //
    // Within a phase systems run in an order that satisfies their before / after sets (registration order
    // where that leaves a choice); their Commands are applied at the end of the phase, in the same order.
    class Schedule
    {
    public:
        // Name comes from the function itself
        template<auto Fn>
        System& add(Phase phase)
        {
            constexpr std::string_view name =
                std::define_static_string(std::meta::identifier_of(std::meta::reflect_function(*Fn)));
            return add(phase, name, Fn);
        }

        template<typename F>
        System& add(Phase phase, std::string_view name, F&& fn)
        {
            using Fn = std::remove_cvref_t<F>;
            using Params = typename detail::callable_params<std::remove_pointer_t<Fn>>::type;

            System& system = systems.emplace_back();
            system.name = name;
            system.phase = phase;
            bind(system, std::forward<F>(fn), std::type_identity<Params>{});
            order_dirty = true;
            return system;
        }

        // One frame: run_fixed, then Update and Late
        void run_frame(Registry& registry, double real_dt);
        // As many fixed ticks as real time requires (at most max_ticks_per_frame); updates FrameTime
        void run_fixed(Registry& registry, double real_dt);
        void run_phase(Registry& registry, Phase phase);

        // Systems annotated with [[=ecs::system<...>]] and registered with ECS_REGISTER (ecs_macros.h)
        void add_auto_systems();

        // In registration order
        const std::deque<System>& get_systems() const { return systems; }
        // In execution order
        std::span<System* const> get_phase_systems(Phase phase) const;

        std::vector<Ambiguity> find_ambiguities() const;

        double fixed_dt = 1.0 / 60.0;
        // After a hitch, drop time instead of simulating a growing backlog
        uint32_t max_ticks_per_frame = 4;

    private:
        template<typename F, typename... P>
        static void bind(System& system, F&& fn, std::type_identity<std::tuple<P...>>)
        {
            (SystemParam<std::remove_cvref_t<P>>::access(system.access), ...);
            // per system state of the parameters that keep one (EventReader cursors)
            using States = std::tuple<typename detail::param_state<std::remove_cvref_t<P>>::type...>;
            system.run = [fn = std::forward<F>(fn), states = States{}](SystemContext& c) mutable
            {
                std::apply([&] (auto&... state)
                {
                    std::invoke(fn, detail::fetch_param<std::remove_cvref_t<P>>(c, state)...);
                }, states);
            };
        }

        // Topological sort of every phase by before / after
        void build_order() const;

        std::deque<System> systems;
        mutable std::array<std::vector<System*>, size_t(Phase::Count)> phase_order;
        mutable bool order_dirty = true;
        double accumulator = 0.0;
    };

    // Annotations of a system that registers itself: no central list of systems.
    //
    //     struct TransformPropagation {};   // a set: any type, used as a label
    //
    //     namespace
    //     {
    //         [[=ecs::system<Phase::Late>, =ecs::in_set<TransformPropagation>]]
    //         void propagate_transforms(Registry& registry) { ... }
    //
    //         [[=ecs::system<Phase::Late>, =ecs::after<TransformPropagation>]]
    //         void sync_render(Query<const WorldTransform, const Mesh> meshes) { ... }
    //
    //         [[=ecs::system<Phase::PostLoad>, =ecs::system<Phase::Late>]]   // runs in both phases
    //         void create_colliders(Registry& registry) { ... }
    //
    //         ECS_REGISTER()   // after the systems, in the same namespace
    //     }
    //
    // Order inside a phase comes only from before / after. Where they leave a choice the order is by source
    // file and name (deterministic, but not the declaration order), and Schedule::find_ambiguities reports
    // systems whose access conflicts without an order between them. ambiguous_with<Set> marks such a
    // conflict as intended.
    template<Phase P>
    struct SystemTag {};
    template<Phase P>
    inline constexpr SystemTag<P> system;

    template<typename Set>
    struct InSet {};
    template<typename Set>
    inline constexpr InSet<Set> in_set;

    template<typename Set>
    struct Before {};
    template<typename Set>
    inline constexpr Before<Set> before;

    template<typename Set>
    struct After {};
    template<typename Set>
    inline constexpr After<Set> after;

    template<typename Set>
    struct AmbiguousWith {};
    template<typename Set>
    inline constexpr AmbiguousWith<Set> ambiguous_with;

    // Component hooks found by the same ECS_REGISTER, installed into a registry by add_auto_hooks:
    //
    //     [[=ecs::on_remove]]
    //     void destroy_body(Registry& registry, Entity e, MeshCollider& collider) { ... }
    //
    // The component type is the third parameter. See Registry::on_add / on_remove.
    struct OnAddTag {};
    inline constexpr OnAddTag on_add;
    struct OnRemoveTag {};
    inline constexpr OnRemoveTag on_remove;

    // Hooks annotated with [[=ecs::on_add]] / [[=ecs::on_remove]] and registered with ECS_REGISTER
    void add_auto_hooks(Registry& registry);

    namespace detail
    {
        // A system (add_system) or a hook (add_hook) found by ECS_REGISTER
        struct AutoSystem
        {
            std::string_view file;
            std::string_view name;
            uint32_t index = 0;     // annotations of one function
            void (*add_system)(Schedule&) = nullptr;
            void (*add_hook)(Registry&) = nullptr;
        };

        void register_auto_system(const AutoSystem& system);

        struct SystemDecl
        {
            std::meta::info function;
            Phase phase;
        };

        consteval std::meta::info annotation_type(std::meta::info annotation)
        {
            return std::meta::dealias(std::meta::remove_cv(std::meta::type_of(annotation)));
        }

        consteval bool is_annotation_of(std::meta::info type, std::meta::info tag_template)
        {
            return std::meta::has_template_arguments(type) && std::meta::template_of(type) == tag_template;
        }

        // One entry per ecs::system annotation of the functions of a namespace
        consteval std::vector<SystemDecl> annotated_systems(std::meta::info ns)
        {
            std::vector<SystemDecl> result;
            for (std::meta::info member : std::meta::members_of(ns, std::meta::access_context::unchecked()))
            {
                if (!std::meta::is_function(member))
                    continue;
                for (std::meta::info annotation : std::meta::annotations_of(member))
                {
                    const std::meta::info type = annotation_type(annotation);
                    if (is_annotation_of(type, ^^SystemTag))
                        result.push_back({ member, std::meta::extract<Phase>(std::meta::template_arguments_of(type)[0]) });
                }
            }
            return result;
        }

        // Set names of the in_set / before / after / ambiguous_with (tag_template) annotations of a function
        consteval std::vector<const char*> annotated_sets(std::meta::info function, std::meta::info tag_template)
        {
            std::vector<const char*> result;
            for (std::meta::info annotation : std::meta::annotations_of(function))
            {
                const std::meta::info type = annotation_type(annotation);
                if (is_annotation_of(type, tag_template))
                    result.push_back(std::define_static_string(qualified_name(std::meta::template_arguments_of(type)[0])));
            }
            return result;
        }

        inline void assign_sets(std::vector<std::string_view>& to, std::span<const char* const> names)
        {
            to.assign(names.begin(), names.end());
        }

        struct HookDecl
        {
            std::meta::info function;
            bool on_remove;
        };

        // One entry per ecs::on_add / ecs::on_remove annotation of the functions of a namespace
        consteval std::vector<HookDecl> annotated_hooks(std::meta::info ns)
        {
            std::vector<HookDecl> result;
            for (std::meta::info member : std::meta::members_of(ns, std::meta::access_context::unchecked()))
            {
                if (!std::meta::is_function(member))
                    continue;
                for (std::meta::info annotation : std::meta::annotations_of(member))
                {
                    const std::meta::info type = annotation_type(annotation);
                    if (type == ^^OnAddTag || type == ^^OnRemoveTag)
                        result.push_back({ member, type == ^^OnRemoveTag });
                }
            }
            return result;
        }

        // Marker: a type declared by ECS_REGISTER in the namespace of the systems.
        // Templates are keyed by it, not by std::meta::info (specializations differing only in an info
        // argument get the same mangled name in clang-p2996 and are merged by the linker).
        template<typename Marker>
        struct SystemsOf
        {
            static constexpr std::span<const SystemDecl> decls =
                std::define_static_array(annotated_systems(std::meta::parent_of(^^Marker)));
            static constexpr std::span<const HookDecl> hooks =
                std::define_static_array(annotated_hooks(std::meta::parent_of(^^Marker)));
        };

        template<typename Marker, size_t I>
        struct SystemAt
        {
            static constexpr SystemDecl decl = SystemsOf<Marker>::decls[I];
            static constexpr auto function = &[:decl.function:];
            static constexpr Phase phase = decl.phase;
            static constexpr std::string_view name = std::define_static_string(std::meta::identifier_of(decl.function));
            static constexpr std::span<const char* const> sets = std::define_static_array(annotated_sets(decl.function, ^^InSet));
            static constexpr std::span<const char* const> before = std::define_static_array(annotated_sets(decl.function, ^^Before));
            static constexpr std::span<const char* const> after = std::define_static_array(annotated_sets(decl.function, ^^After));
            static constexpr std::span<const char* const> ambiguous_with =
                std::define_static_array(annotated_sets(decl.function, ^^AmbiguousWith));

            static void add(Schedule& schedule)
            {
                System& system = schedule.add<function>(phase);
                assign_sets(system.sets, sets);
                assign_sets(system.before, before);
                assign_sets(system.after, after);
                assign_sets(system.ambiguous_with, ambiguous_with);
            }
        };

        template<typename Marker, size_t I>
        struct HookAt
        {
            static constexpr HookDecl decl = SystemsOf<Marker>::hooks[I];
            static constexpr auto function = &[:decl.function:];
            static constexpr bool on_remove = decl.on_remove;
            static constexpr std::string_view name = std::define_static_string(std::meta::identifier_of(decl.function));

            using Params = typename callable_params<std::remove_pointer_t<decltype(function)>>::type;
            static_assert(std::tuple_size_v<Params> == 3, "component hook: void(Registry&, Entity, T& component)");
            using T = std::remove_cvref_t<std::tuple_element_t<2, Params>>;

            static void add(Registry& registry)
            {
                if constexpr (on_remove)
                    registry.on_remove<T>(function);
                else
                    registry.on_add<T>(function);
            }
        };

        template<typename Marker, size_t... S, size_t... H>
        bool register_systems(std::string_view file, std::index_sequence<S...>, std::index_sequence<H...>)
        {
            (register_auto_system({ file, SystemAt<Marker, S>::name, uint32_t(S), &SystemAt<Marker, S>::add, nullptr }), ...);
            (register_auto_system({ file, HookAt<Marker, H>::name, uint32_t(H), nullptr, &HookAt<Marker, H>::add }), ...);
            return true;
        }

        template<typename Marker>
        bool register_systems(std::string_view file)
        {
            constexpr size_t systems = SystemsOf<Marker>::decls.size();
            constexpr size_t hooks = SystemsOf<Marker>::hooks.size();
            static_assert(systems + hooks > 0,
                "ECS_REGISTER: no [[=ecs::system<...>]] / [[=ecs::on_add]] / [[=ecs::on_remove]] functions declared before it in this namespace");
            return register_systems<Marker>(file, std::make_index_sequence<systems>{}, std::make_index_sequence<hooks>{});
        }
    }
}
