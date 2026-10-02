export module ai:brain;

import std.compat;

// How a brain is put together (AI4): a hybrid of modes and actions.
//
//   modes      a small state machine per species (idle / alert / combat / flee ...), decided a few times per
//              second (ThinkTimer: the agents are spread over the ticks) from what the agent knows
//   actions    what it does now (rest, move to, leap, howl), one at a time, run every fixed tick by an
//              ActionRunner. Actions only carry out: the brain picks them. An action is a struct:
//                  static constexpr const char* name;
//                  ActionStatus tick(Context&, float dt);          running / succeeded / failed
//                  void start(Context&);                           optional
//                  void stop(Context&);                            optional: always called when it ends or is
//                                                                  replaced - releases what it holds (tokens)
//                  bool interruptible(const Context&) const;       optional (default true): false in a committed
//                                                                  phase (a leap in the air)
//              Short actions are explicit phases; long scripted sequences may become coroutines later (AI13) -
//              to the runner they are one more action type.
//   log        DecisionLog: the last decisions with their reasons, for Windows > AI
//   debug      BrainDebug: mode / action / target lines any brain fills for the debug UI
export namespace ai
{
    enum class ActionStatus : uint8_t { running, succeeded, failed };

    template<typename Context, typename... Actions>
    class ActionRunner
    {
    public:
        bool idle() const { return current.index() == 0; }

        template<typename A>
        bool is() const { return std::holds_alternative<A>(current); }

        template<typename A>
        A* get() { return std::get_if<A>(&current); }

        template<typename A>
        const A* get() const { return std::get_if<A>(&current); }

        // can the brain replace it now
        bool interruptible(const Context& ctx) const
        {
            return std::visit([&] (const auto& a) -> bool {
                if constexpr (requires { a.interruptible(ctx); })
                    return a.interruptible(ctx);
                else
                    return true;
            }, current);
        }

        // stops the running action (if any), starts this one
        template<typename A>
        A& start(Context& ctx, A action)
        {
            stop(ctx);
            A& a = current.template emplace<A>(std::move(action));
            if constexpr (requires { a.start(ctx); })
                a.start(ctx);
            return a;
        }

        // one tick of the running action; a finished one is stopped (the runner is idle then)
        ActionStatus tick(Context& ctx, float dt)
        {
            last = std::visit([&] (auto& a) -> ActionStatus {
                if constexpr (std::is_same_v<std::decay_t<decltype(a)>, std::monostate>)
                    return ActionStatus::succeeded;
                else
                    return a.tick(ctx, dt);
            }, current);
            if (last != ActionStatus::running)
                stop(ctx);
            return last;
        }

        void stop(Context& ctx)
        {
            std::visit([&] (auto& a) {
                if constexpr (requires { a.stop(ctx); })
                    a.stop(ctx);
            }, current);
            current.template emplace<0>();
        }

        const char* name() const
        {
            return std::visit([] (const auto& a) -> const char* {
                using A = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<A, std::monostate>)
                    return "none";
                else
                    return A::name;
            }, current);
        }

        // how the last action ended (running while one runs)
        ActionStatus last_status() const { return last; }

    private:
        std::variant<std::monostate, Actions...> current;
        ActionStatus last = ActionStatus::succeeded;
    };

    // The last decisions of a brain with their reasons ("combat: saw cosmo_bunny"), oldest first
    class DecisionLog
    {
    public:
        static constexpr size_t capacity = 16;

        struct Entry
        {
            double time = 0.0;   // SimTime
            std::string text;
        };

        void add(double time, std::string text)
        {
            entries[next] = { time, std::move(text) };
            next = (next + 1) % capacity;
            count = std::min(count + 1, capacity);
        }

        template<typename F>
        void each(F&& f) const
        {
            for (size_t i = 0; i < count; ++i)
                f(entries[(next + capacity - count + i) % capacity]);
        }

        size_t size() const { return count; }

    private:
        std::array<Entry, capacity> entries;
        size_t next = 0;
        size_t count = 0;
    };

    // A brain thinks every `interval` s; the first wait is spread by `spread` (0..1, e.g. from the entity index) so
    // the agents of a pack do not all think on the same tick
    struct ThinkTimer
    {
        float interval = 0.1f;
        float wait = -1.0f;   // < 0: not started

        bool due(float dt, float spread)
        {
            if (wait < 0.0f)
                wait = interval * std::clamp(spread, 0.0f, 1.0f);
            wait -= dt;
            if (wait > 0.0f)
                return false;
            wait += interval;
            if (wait <= 0.0f)
                wait = interval;
            return true;
        }

        // on the next tick (something happened that can't wait: hit, target lost)
        void hurry() { wait = 0.0f; }
    };

    // What a brain shows in Windows > AI (any species fills it)
    struct BrainDebug
    {
        std::string mode;
        std::string action;
        std::string target;
        std::string detail;
        DecisionLog log;
    };
}
