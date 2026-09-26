export module ecs:commands;

import std.compat;

import :entity;
import :component;
import :registry;

export namespace ecs
{
    // Deferred structural changes, recorded by systems and applied in order at the end of the phase.
    // spawn() returns a usable handle right away; the entity becomes alive when the buffer is applied.
    // Commands on an entity that is dead by then are skipped.
    class Commands
    {
    public:
        explicit Commands(Registry& in_registry)
            : registry(&in_registry)
        {}

        Commands(Commands&&) = default;
        Commands& operator=(Commands&&) = default;

        Entity spawn()
        {
            const Entity e = registry->reserve();
            ops.push_back([e](Registry& r) { r.commit_reserved(e); });
            return e;
        }

        template<typename... Cs>
        Entity spawn(Cs&&... components)
        {
            const Entity e = spawn();
            (add(e, std::forward<Cs>(components)), ...);
            return e;
        }

        void destroy(Entity e)
        {
            ops.push_back([e](Registry& r) { r.destroy(e); });
        }

        template<typename T>
        void add(Entity e, T&& component)
        {
            using C = std::remove_cvref_t<T>;
            ops.push_back([e, c = C(std::forward<T>(component))](Registry& r) mutable
            {
                if (r.alive(e))
                    r.add<C>(e, std::move(c));
            });
        }

        template<Component T>
        void remove(Entity e)
        {
            ops.push_back([e](Registry& r)
            {
                if (r.alive(e))
                    r.remove<T>(e);
            });
        }

        // Arbitrary deferred work with full registry access
        void run(std::move_only_function<void(Registry&)> fn)
        {
            ops.push_back(std::move(fn));
        }

        void apply()
        {
            auto pending = std::move(ops);
            ops.clear();
            for (auto& op : pending)
                op(*registry);
        }

        bool empty() const { return ops.empty(); }
        Registry& get_registry() const { return *registry; }

    private:
        Registry* registry;
        std::vector<std::move_only_function<void(Registry&)>> ops;
    };
}
