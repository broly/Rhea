export module ecs:schedule;

import std.compat;

import :entity;
import :component;
import :registry;
import :query;
import :commands;

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

    struct SystemContext
    {
        Registry& registry;
        Commands& commands;
    };

    // How a system parameter type is fetched and what it accesses. Specialize to add parameter kinds.
    template<typename P>
    struct SystemParam
    {
        static_assert(false, "unsupported system parameter: use Query<...>, Commands&, Res<T>, ResMut<T> or Registry&");
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
        static void access(Access& a) { a.resource_reads.push_back(Registry::resource_key<T>()); }
    };

    template<typename T>
    struct SystemParam<ResMut<T>>
    {
        static ResMut<T> fetch(SystemContext& c) { return ResMut<T>(c.registry.resource<T>()); }
        static void access(Access& a) { a.resource_writes.push_back(Registry::resource_key<T>()); }
    };

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

    struct System
    {
        std::string name;
        Phase phase = Phase::Update;
        Access access;
        std::move_only_function<void(SystemContext&)> run;
        double last_ms = 0.0;
    };

    // Systems are plain functions (or non-generic lambdas) whose parameters say what they need:
    //
    //     void apply_velocity(Query<Transform, const Velocity> q, Res<SimTime> time) { ... }
    //     schedule.add<&apply_velocity>(Phase::Fixed);
    //     schedule.add(Phase::Update, "spin", [](Query<Transform> q) { ... });
    //
    // Within a phase systems run in registration order; their Commands are applied at the end of the phase.
    class Schedule
    {
    public:
        // Name comes from the function itself
        template<auto Fn>
        void add(Phase phase)
        {
            constexpr std::string_view name =
                std::define_static_string(std::meta::identifier_of(std::meta::reflect_function(*Fn)));
            add(phase, name, Fn);
        }

        template<typename F>
        void add(Phase phase, std::string_view name, F&& fn)
        {
            using Fn = std::remove_cvref_t<F>;
            using Params = typename detail::callable_params<std::remove_pointer_t<Fn>>::type;

            System& system = systems.emplace_back();
            system.name = name;
            system.phase = phase;
            bind(system, std::forward<F>(fn), std::type_identity<Params>{});
        }

        // One frame: run_fixed, then Update and Late
        void run_frame(Registry& registry, double real_dt);
        // As many fixed ticks as real time requires (at most max_ticks_per_frame); updates FrameTime
        void run_fixed(Registry& registry, double real_dt);
        void run_phase(Registry& registry, Phase phase);

        const std::deque<System>& get_systems() const { return systems; }

        double fixed_dt = 1.0 / 60.0;
        // After a hitch, drop time instead of simulating a growing backlog
        uint32_t max_ticks_per_frame = 4;

    private:
        template<typename F, typename... P>
        static void bind(System& system, F&& fn, std::type_identity<std::tuple<P...>>)
        {
            (SystemParam<std::remove_cvref_t<P>>::access(system.access), ...);
            system.run = [fn = std::forward<F>(fn)](SystemContext& c) mutable
            {
                std::invoke(fn, SystemParam<std::remove_cvref_t<P>>::fetch(c)...);
            };
        }

        std::deque<System> systems;
        double accumulator = 0.0;
    };
}
