module;

#include "common/assertion_macros.h"

export module ecs:registry;

import std.compat;
import assertions;

import :entity;
import :component;

export namespace ecs
{
    // One component type's array inside an archetype. Rows are parallel across the columns.
    struct Column
    {
        const ComponentInfo* info = nullptr;
        std::byte* data = nullptr;  // nullptr for tags

        void* at(size_t row) const { return data + row * info->size; }
    };

    // All entities with exactly the same set of components, stored as structure-of-arrays.
    // Adding / removing a component moves the entity to another archetype.
    class Archetype
    {
    public:
        Archetype(uint32_t index, std::vector<ComponentId> sorted_types);
        ~Archetype();
        Archetype(const Archetype&) = delete;
        Archetype& operator=(const Archetype&) = delete;

        uint32_t get_index() const { return index; }
        std::span<const ComponentId> get_types() const { return types; }
        std::span<const Column> get_columns() const { return columns; }
        std::span<const Entity> get_entities() const { return entities; }
        size_t size() const { return entities.size(); }

        // -1 if the archetype has no such component
        int32_t find_column(ComponentId id) const;
        bool has_all(std::span<const ComponentId> sorted_ids) const;

        // Appends a row with uninitialized component storage; returns the row
        uint32_t push_row(Entity e);
        // Removes a row whose components are already relocated / destroyed by the caller,
        // moving the last row into its place. Returns the entity moved into `row` (null if none).
        Entity swap_remove(uint32_t row);

    private:
        friend class Registry;

        void grow(size_t new_capacity);

        uint32_t index;
        std::vector<ComponentId> types;
        std::vector<Column> columns;
        std::vector<Entity> entities;
        size_t capacity = 0;
        std::unordered_map<ComponentId, Archetype*> add_edges;
        std::unordered_map<ComponentId, Archetype*> remove_edges;
    };

    // Entities, their components and global resources of one simulation.
    // Structural changes (create / destroy / add / remove) are not allowed while a Query iterates:
    // systems use Commands, which are applied at the end of the phase.
    class Registry
    {
    public:
        Registry();
        ~Registry();
        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;

        // --- entities ---

        Entity create();
        void destroy(Entity e);
        bool alive(Entity e) const;
        size_t entity_count() const { return alive_count; }

        // Allocates a handle that becomes alive on commit_reserved (Commands::spawn)
        Entity reserve();
        void commit_reserved(Entity e);

        // --- components ---

        // Constructs the component, or replaces the existing one
        template<Component T, typename... Args>
        T& add(Entity e, Args&&... args)
        {
            const ComponentId id = component_id<T>();
            if (void* existing = get_raw(e, id))
            {
                if constexpr (std::is_empty_v<T>)
                    return detail::tag_instance<T>();
                else
                    return *static_cast<T*>(existing) = T(std::forward<Args>(args)...);
            }

            T* result;
            if constexpr (std::is_empty_v<T>)
            {
                add_raw(e, id);
                result = &detail::tag_instance<T>();
            }
            else
            {
                result = std::construct_at(static_cast<T*>(add_raw(e, id)), std::forward<Args>(args)...);
            }
            fire_hooks(add_hooks, id, e, result);
            return *result;
        }

        template<Component T>
        void remove(Entity e)
        {
            remove_raw(e, component_id<T>());
        }

        template<Component T>
        T* get(Entity e)
        {
            void* p = get_raw(e, component_id<T>());
            if constexpr (std::is_empty_v<T>)
                return p ? &detail::tag_instance<T>() : nullptr;
            else
                return static_cast<T*>(p);
        }

        template<Component T>
        const T* get(Entity e) const
        {
            return const_cast<Registry*>(this)->get<T>(e);
        }

        template<Component T>
        bool has(Entity e) const
        {
            return get_raw(e, component_id<T>()) != nullptr;
        }

        // Type-erased access (debug UI, serialization): nullptr if absent, non-null dummy for tags
        void* get_raw(Entity e, ComponentId id) const;
        std::span<const ComponentId> get_component_ids(Entity e) const;

        // --- hooks ---
        // Called right after a component is added, and right before it is removed
        // (also when its entity is destroyed). Must not change the entity's component set.

        template<Component T, typename F>
        void on_add(F&& fn)
        {
            add_hook<T>(add_hooks, std::forward<F>(fn));
        }

        template<Component T, typename F>
        void on_remove(F&& fn)
        {
            add_hook<T>(remove_hooks, std::forward<F>(fn));
        }

        // --- resources: one global instance per type ---

        template<typename T, typename... Args>
        T& set_resource(Args&&... args)
        {
            auto& slot = resources[resource_key<T>()];
            if (slot.ptr)
                slot.deleter(slot.ptr);
            slot.ptr = new T(std::forward<Args>(args)...);
            slot.deleter = [](void* p) { delete static_cast<T*>(p); };
            return *static_cast<T*>(slot.ptr);
        }

        // Resource owned elsewhere (engine services: input, physics, scene view): the registry only refers to
        // it, so systems get it as Res<T> / ResMut<T>. It must outlive its use by systems and hooks.
        // A const object is registered as T too: read it with Res<T> only.
        template<typename T>
        T& set_resource_ref(T& external)
        {
            auto& slot = resources[resource_key<std::remove_const_t<T>>()];
            if (slot.ptr)
                slot.deleter(slot.ptr);
            slot.ptr = const_cast<std::remove_const_t<T>*>(&external);
            slot.deleter = [](void*) {};
            return external;
        }

        template<typename T>
        T* find_resource() const
        {
            auto it = resources.find(resource_key<T>());
            return it != resources.end() ? static_cast<T*>(it->second.ptr) : nullptr;
        }

        template<typename T>
        T& resource() const
        {
            T* r = find_resource<T>();
            checkf(r, "missing resource %s", std::string(detail::type_name<T>()).c_str());
            return *r;
        }

        template<typename T>
        static const void* resource_key()
        {
            static const char key = 0;
            return &key;
        }

        // --- archetypes / queries ---

        std::span<const std::unique_ptr<Archetype>> get_archetypes() const { return archetypes; }

        // Archetypes containing all of `sorted_ids` (cached, extended as archetypes appear)
        const std::vector<Archetype*>& match(std::span<const ComponentId> sorted_ids);

        // Marks a query iteration in progress: structural changes assert meanwhile
        struct IterationScope
        {
            explicit IterationScope(Registry& r) : registry(r) { ++registry.iterating; }
            ~IterationScope() { --registry.iterating; }
            Registry& registry;
        };

    private:
        struct Record
        {
            uint32_t generation = 0;
            Archetype* archetype = nullptr;   // nullptr: free or reserved
            uint32_t row = 0;
            bool reserved = false;
        };

        struct QueryCache
        {
            std::vector<Archetype*> archetypes;
            size_t checked = 0;  // archetypes[0, checked) are already tested
        };

        struct ResourceSlot
        {
            void* ptr = nullptr;
            void (*deleter)(void*) = nullptr;
        };

        using Hook = std::function<void(Registry&, Entity, void*)>;
        using HookTable = std::vector<std::vector<Hook>>;  // by ComponentId

        const Record* find_record(Entity e) const;
        Entity allocate();
        void release(Entity e);

        // Moves the entity to the archetype with `id` added; returns uninitialized storage for it (nullptr for tags)
        void* add_raw(Entity e, ComponentId id);
        void remove_raw(Entity e, ComponentId id);
        void move_entity(Entity e, Archetype* target);

        Archetype* archetype_with(Archetype* from, ComponentId id);
        Archetype* archetype_without(Archetype* from, ComponentId id);
        Archetype* get_or_create_archetype(std::vector<ComponentId> sorted_types);

        void fire_hooks(const HookTable& table, ComponentId id, Entity e, void* component);
        void check_not_iterating() const;

        template<Component T, typename F>
        void add_hook(HookTable& table, F&& fn)
        {
            const ComponentId id = component_id<T>();
            if (table.size() <= id)
                table.resize(id + 1);
            table[id].push_back([fn = std::forward<F>(fn)](Registry& r, Entity e, void* c) mutable
            {
                if constexpr (std::is_empty_v<T>)
                    fn(r, e, detail::tag_instance<T>());
                else
                    fn(r, e, *static_cast<T*>(c));
            });
        }

        std::vector<Record> records;
        std::vector<uint32_t> free_indices;
        size_t alive_count = 0;

        std::vector<std::unique_ptr<Archetype>> archetypes;
        std::map<std::vector<ComponentId>, Archetype*> archetype_by_types;
        Archetype* root = nullptr;  // no components

        std::map<std::vector<ComponentId>, QueryCache> query_caches;

        HookTable add_hooks;
        HookTable remove_hooks;

        std::unordered_map<const void*, ResourceSlot> resources;

        int32_t iterating = 0;
    };
}
