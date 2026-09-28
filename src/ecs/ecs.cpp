module;

#include "common/assertion_macros.h"

module ecs;

import std.compat;
import assertions;

import :entity;
import :component;
import :registry;
import :commands;
import :schedule;

namespace ecs
{
    // ------------------------------------------------------------------ components

    namespace
    {
        struct ComponentTable
        {
            std::mutex mutex;
            std::deque<ComponentInfo> infos;  // deque: references stay valid while it grows
        };

        ComponentTable& component_table()
        {
            static ComponentTable table;
            return table;
        }
    }

    ComponentId detail::register_component(ComponentInfo info)
    {
        ComponentTable& table = component_table();
        std::scoped_lock lock(table.mutex);
        info.id = ComponentId(table.infos.size());
        table.infos.push_back(info);
        return info.id;
    }

    const ComponentInfo& component_info(ComponentId id)
    {
        ComponentTable& table = component_table();
        std::scoped_lock lock(table.mutex);
        return table.infos[id];
    }

    size_t registered_component_count()
    {
        ComponentTable& table = component_table();
        std::scoped_lock lock(table.mutex);
        return table.infos.size();
    }

    // ------------------------------------------------------------------ archetype

    Archetype::Archetype(uint32_t in_index, std::vector<ComponentId> sorted_types)
        : index(in_index)
        , types(std::move(sorted_types))
    {
        columns.reserve(types.size());
        for (ComponentId id : types)
            columns.push_back(Column{ .info = &component_info(id) });
    }

    Archetype::~Archetype()
    {
        for (Column& column : columns)
        {
            if (!column.data)
                continue;
            for (size_t row = 0; row < entities.size(); ++row)
                column.info->destroy(column.at(row));
            ::operator delete(column.data, std::align_val_t(column.info->align));
        }
    }

    int32_t Archetype::find_column(ComponentId id) const
    {
        auto it = std::ranges::lower_bound(types, id);
        return it != types.end() && *it == id ? int32_t(it - types.begin()) : -1;
    }

    bool Archetype::has_all(std::span<const ComponentId> sorted_ids) const
    {
        return std::ranges::includes(types, sorted_ids);
    }

    uint32_t Archetype::push_row(Entity e)
    {
        if (entities.size() == capacity)
            grow(std::max<size_t>(16, capacity * 2));
        entities.push_back(e);
        return uint32_t(entities.size() - 1);
    }

    Entity Archetype::swap_remove(uint32_t row)
    {
        const uint32_t last = uint32_t(entities.size() - 1);
        Entity moved = null_entity;
        if (row != last)
        {
            for (Column& column : columns)
            {
                if (column.data)
                    column.info->relocate(column.at(row), column.at(last));
            }
            moved = entities[last];
            entities[row] = moved;
        }
        entities.pop_back();
        return moved;
    }

    void Archetype::grow(size_t new_capacity)
    {
        for (Column& column : columns)
        {
            const ComponentInfo& info = *column.info;
            if (info.size == 0)
                continue;

            auto* data = static_cast<std::byte*>(::operator new(info.size * new_capacity, std::align_val_t(info.align)));
            for (size_t row = 0; row < entities.size(); ++row)
                info.relocate(data + row * info.size, column.at(row));
            if (column.data)
                ::operator delete(column.data, std::align_val_t(info.align));
            column.data = data;
        }
        capacity = new_capacity;
    }

    // ------------------------------------------------------------------ registry

    Registry::Registry()
    {
        root = get_or_create_archetype({});
    }

    Registry::~Registry()
    {
        // archetypes destroy their components; hooks are not fired on shutdown
        archetypes.clear();
        for (auto& [key, slot] : resources)
            slot.deleter(slot.ptr);
    }

    const Registry::Record* Registry::find_record(Entity e) const
    {
        if (e.index >= records.size())
            return nullptr;
        const Record& record = records[e.index];
        return record.generation == e.generation ? &record : nullptr;
    }

    Entity Registry::allocate()
    {
        uint32_t index;
        if (!free_indices.empty())
        {
            index = free_indices.back();
            free_indices.pop_back();
        }
        else
        {
            index = uint32_t(records.size());
            records.emplace_back();
        }
        return Entity{ index, records[index].generation };
    }

    void Registry::release(Entity e)
    {
        Record& record = records[e.index];
        record.archetype = nullptr;
        record.reserved = false;
        ++record.generation;
        free_indices.push_back(e.index);
    }

    Entity Registry::create()
    {
        check_not_iterating();
        const Entity e = allocate();
        Record& record = records[e.index];
        record.archetype = root;
        record.row = root->push_row(e);
        ++alive_count;
        return e;
    }

    Entity Registry::reserve()
    {
        const Entity e = allocate();
        records[e.index].reserved = true;
        return e;
    }

    void Registry::commit_reserved(Entity e)
    {
        check_not_iterating();
        const Record* found = find_record(e);
        if (!found || !found->reserved)
            return;  // destroyed before commit
        Record& record = records[e.index];
        record.reserved = false;
        record.archetype = root;
        record.row = root->push_row(e);
        ++alive_count;
    }

    bool Registry::alive(Entity e) const
    {
        const Record* record = find_record(e);
        return record && record->archetype;
    }

    void Registry::destroy(Entity e)
    {
        check_not_iterating();
        const Record* found = find_record(e);
        if (!found)
            return;
        if (found->reserved)
        {
            release(e);
            return;
        }
        if (!found->archetype)
            return;

        // hooks first, while the components are intact; hooks may add entities, so re-read the record after
        {
            const std::vector<ComponentId> types(found->archetype->types);
            for (ComponentId id : types)
                fire_hooks(remove_hooks, id, e, get_raw(e, id));
        }

        Record& record = records[e.index];
        Archetype* archetype = record.archetype;
        for (Column& column : archetype->columns)
        {
            if (column.data)
                column.info->destroy(column.at(record.row));
        }
        if (Entity moved = archetype->swap_remove(record.row))
            records[moved.index].row = record.row;

        release(e);
        --alive_count;
    }

    void* Registry::get_raw(Entity e, ComponentId id) const
    {
        const Record* record = find_record(e);
        if (!record || !record->archetype)
            return nullptr;
        const int32_t column = record->archetype->find_column(id);
        if (column < 0)
            return nullptr;
        const Column& c = record->archetype->columns[column];
        // tags: any non-null pointer means "present"; typed accessors substitute the tag instance
        return c.data ? c.at(record->row) : static_cast<void*>(const_cast<Archetype*>(record->archetype));
    }

    std::span<const ComponentId> Registry::get_component_ids(Entity e) const
    {
        const Record* record = find_record(e);
        if (!record || !record->archetype)
            return {};
        return record->archetype->types;
    }

    void* Registry::add_raw(Entity e, ComponentId id)
    {
        check_not_iterating();
        const Record* record = find_record(e);
        checkf(record && record->archetype, "add component %s to a dead entity", std::string(component_info(id).name).c_str());

        Archetype* target = archetype_with(record->archetype, id);
        move_entity(e, target);
        const Column& column = target->columns[target->find_column(id)];
        return column.data ? column.at(records[e.index].row) : nullptr;
    }

    void Registry::remove_raw(Entity e, ComponentId id)
    {
        check_not_iterating();
        void* component = get_raw(e, id);
        if (!component)
            return;
        fire_hooks(remove_hooks, id, e, component);

        // the hook must not change the entity's component set, but the record may have moved in memory
        Record& record = records[e.index];
        Archetype* target = archetype_without(record.archetype, id);
        move_entity(e, target);
    }

    void Registry::move_entity(Entity e, Archetype* target)
    {
        Record& record = records[e.index];
        Archetype* source = record.archetype;
        const uint32_t source_row = record.row;
        const uint32_t target_row = target->push_row(e);

        for (size_t i = 0; i < source->types.size(); ++i)
        {
            Column& from = source->columns[i];
            if (!from.data)
                continue;
            const int32_t j = target->find_column(source->types[i]);
            if (j >= 0)
                from.info->relocate(target->columns[j].at(target_row), from.at(source_row));
            else
                from.info->destroy(from.at(source_row));
        }

        if (Entity moved = source->swap_remove(source_row))
            records[moved.index].row = source_row;

        record.archetype = target;
        record.row = target_row;
    }

    Archetype* Registry::archetype_with(Archetype* from, ComponentId id)
    {
        if (auto it = from->add_edges.find(id); it != from->add_edges.end())
            return it->second;

        std::vector<ComponentId> types(from->types);
        types.insert(std::ranges::lower_bound(types, id), id);
        Archetype* to = get_or_create_archetype(std::move(types));
        from->add_edges[id] = to;
        to->remove_edges[id] = from;
        return to;
    }

    Archetype* Registry::archetype_without(Archetype* from, ComponentId id)
    {
        if (auto it = from->remove_edges.find(id); it != from->remove_edges.end())
            return it->second;

        std::vector<ComponentId> types(from->types);
        std::erase(types, id);
        Archetype* to = get_or_create_archetype(std::move(types));
        from->remove_edges[id] = to;
        to->add_edges[id] = from;
        return to;
    }

    Archetype* Registry::get_or_create_archetype(std::vector<ComponentId> sorted_types)
    {
        if (auto it = archetype_by_types.find(sorted_types); it != archetype_by_types.end())
            return it->second;

        auto archetype = std::make_unique<Archetype>(uint32_t(archetypes.size()), sorted_types);
        Archetype* result = archetype.get();
        archetypes.push_back(std::move(archetype));
        archetype_by_types.emplace(std::move(sorted_types), result);
        return result;
    }

    const std::vector<Archetype*>& Registry::match(std::span<const ComponentId> sorted_ids)
    {
        QueryCache& cache = query_caches[std::vector<ComponentId>(sorted_ids.begin(), sorted_ids.end())];
        for (; cache.checked < archetypes.size(); ++cache.checked)
        {
            Archetype* archetype = archetypes[cache.checked].get();
            if (archetype->has_all(sorted_ids))
                cache.archetypes.push_back(archetype);
        }
        return cache.archetypes;
    }

    void Registry::fire_hooks(const HookTable& table, ComponentId id, Entity e, void* component)
    {
        if (id >= table.size() || table[id].empty())
            return;
        // copy: a hook may register hooks
        const std::vector<Hook> hooks = table[id];
        for (const Hook& hook : hooks)
            hook(*this, e, component);
    }

    void Registry::check_not_iterating() const
    {
        checkf(iterating == 0, "%s", "structural change while a Query iterates: record it with Commands instead");
    }

    // ------------------------------------------------------------------ schedule

    std::string_view phase_name(Phase phase)
    {
        switch (phase)
        {
        case Phase::FixedPre:  return "FixedPre";
        case Phase::Fixed:     return "Fixed";
        case Phase::FixedPost: return "FixedPost";
        case Phase::Update:    return "Update";
        case Phase::Late:      return "Late";
        case Phase::PostLoad:  return "PostLoad";
        case Phase::Count:     break;
        }
        return "?";
    }

    namespace
    {
        // filled by static initializers of ECS_REGISTER, before main
        std::vector<detail::AutoSystem>& auto_systems()
        {
            static std::vector<detail::AutoSystem> list;
            return list;
        }
    }

    void detail::register_auto_system(const AutoSystem& system)
    {
        auto_systems().push_back(system);
    }

    void Schedule::add_auto_systems()
    {
        std::vector<detail::AutoSystem> list = auto_systems();
        // static initialization order across files is unspecified: sort for a stable schedule
        std::ranges::sort(list, {}, [] (const detail::AutoSystem& s) { return std::tuple(s.file, s.name, s.index); });
        for (const detail::AutoSystem& s : list)
            if (s.add_system)
                s.add_system(*this);
    }

    void add_auto_hooks(Registry& registry)
    {
        std::vector<detail::AutoSystem> list = auto_systems();
        std::ranges::sort(list, {}, [] (const detail::AutoSystem& s) { return std::tuple(s.file, s.name, s.index); });
        for (const detail::AutoSystem& s : list)
            if (s.add_hook)
                s.add_hook(registry);
    }

    namespace
    {
        bool in_any_set(const System& system, std::span<const std::string_view> sets)
        {
            return std::ranges::any_of(sets, [&] (std::string_view set) { return system.in_set(set); });
        }

        // successors[i]: systems of `list` that must run after list[i]
        std::vector<std::vector<uint32_t>> order_successors(std::span<System* const> list)
        {
            std::vector<std::vector<uint32_t>> successors(list.size());
            for (uint32_t i = 0; i < list.size(); ++i)
            {
                for (uint32_t j = 0; j < list.size(); ++j)
                {
                    if (i != j && (in_any_set(*list[j], list[i]->before) || in_any_set(*list[i], list[j]->after)))
                        successors[i].push_back(j);
                }
            }
            return successors;
        }

        // Empty if the systems may run in any order
        std::string access_conflict(const Access& a, const Access& b)
        {
            if (a.exclusive || b.exclusive)
                return "Registry& (exclusive)";

            std::string result;
            const auto add = [&] (std::string_view name)
            {
                if (!result.empty())
                    result += ", ";
                result += name;
            };
            for (ComponentId id : a.writes)
                if (std::ranges::contains(b.writes, id) || std::ranges::contains(b.reads, id))
                    add(component_info(id).name);
            for (ComponentId id : b.writes)
                if (std::ranges::contains(a.reads, id))
                    add(component_info(id).name);

            const auto key = &Access::Resource::key;
            for (const Access::Resource& r : a.resource_writes)
                if (std::ranges::contains(b.resource_writes, r.key, key) || std::ranges::contains(b.resource_reads, r.key, key))
                    add(r.name);
            for (const Access::Resource& r : b.resource_writes)
                if (std::ranges::contains(a.resource_reads, r.key, key))
                    add(r.name);
            return result;
        }
    }

    void Schedule::build_order() const
    {
        for (int phase = 0; phase < int(Phase::Count); ++phase)
        {
            // registration order: the tie-break where before / after leave a choice
            std::vector<System*> list;
            for (const System& system : systems)
                if (int(system.phase) == phase)
                    list.push_back(const_cast<System*>(&system));   // the schedule owns them

            const std::vector<std::vector<uint32_t>> successors = order_successors(list);
            std::vector<uint32_t> predecessor_count(list.size(), 0);
            for (const auto& next : successors)
                for (uint32_t j : next)
                    ++predecessor_count[j];

            // Kahn's algorithm, always taking the earliest registered ready system
            std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<>> ready;
            for (uint32_t i = 0; i < list.size(); ++i)
                if (predecessor_count[i] == 0)
                    ready.push(i);

            std::vector<System*>& order = phase_order[phase];
            order.clear();
            while (!ready.empty())
            {
                const uint32_t i = ready.top();
                ready.pop();
                order.push_back(list[i]);
                for (uint32_t j : successors[i])
                    if (--predecessor_count[j] == 0)
                        ready.push(j);
            }

            if (order.size() != list.size())
            {
                std::string cycle;
                for (uint32_t i = 0; i < list.size(); ++i)
                {
                    if (predecessor_count[i] == 0)
                        continue;
                    cycle += cycle.empty() ? "" : ", ";
                    cycle += list[i]->name;
                    order.push_back(list[i]);   // still run them, in registration order
                }
                checkf(false, "ecs: before / after cycle in phase %s between: %s",
                    std::string(phase_name(Phase(phase))).c_str(), cycle.c_str());
            }
        }
        order_dirty = false;
    }

    std::span<System* const> Schedule::get_phase_systems(Phase phase) const
    {
        if (order_dirty)
            build_order();
        return phase_order[size_t(phase)];
    }

    std::vector<Ambiguity> Schedule::find_ambiguities() const
    {
        std::vector<Ambiguity> result;
        for (int phase = 0; phase < int(Phase::Count); ++phase)
        {
            const std::span<System* const> list = get_phase_systems(Phase(phase));
            const std::vector<std::vector<uint32_t>> successors = order_successors(list);

            // reachable[i][j]: list[i] runs before list[j] through before / after (list is topologically sorted)
            std::vector<std::vector<bool>> reachable(list.size(), std::vector<bool>(list.size(), false));
            for (size_t i = list.size(); i-- > 0;)
            {
                for (uint32_t j : successors[i])
                {
                    reachable[i][j] = true;
                    if (j > i)
                        for (size_t k = 0; k < list.size(); ++k)
                            if (reachable[j][k])
                                reachable[i][k] = true;
                }
            }

            for (size_t i = 0; i < list.size(); ++i)
            {
                for (size_t j = i + 1; j < list.size(); ++j)
                {
                    const System& a = *list[i];
                    const System& b = *list[j];
                    if (reachable[i][j] || reachable[j][i])
                        continue;
                    if (in_any_set(b, a.ambiguous_with) || in_any_set(a, b.ambiguous_with))
                        continue;
                    std::string conflict = access_conflict(a.access, b.access);
                    if (!conflict.empty())
                        result.push_back({ &a, &b, std::move(conflict) });
                }
            }
        }
        return result;
    }

    void Schedule::run_phase(Registry& registry, Phase phase)
    {
        std::deque<Commands> buffers;
        for (System* system_ptr : get_phase_systems(phase))
        {
            System& system = *system_ptr;
            SystemContext context{ registry, buffers.emplace_back(registry) };
            const auto start = std::chrono::steady_clock::now();
            system.run(context);
            system.last_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
        // sync point: in system order, so the result does not depend on scheduling
        for (Commands& commands : buffers)
            commands.apply();
    }

    void Schedule::run_frame(Registry& registry, double real_dt)
    {
        run_fixed(registry, real_dt);
        run_phase(registry, Phase::Update);
        run_phase(registry, Phase::Late);
    }

    void Schedule::run_fixed(Registry& registry, double real_dt)
    {
        SimTime* sim = registry.find_resource<SimTime>();
        if (!sim)
            sim = &registry.set_resource<SimTime>();
        FrameTime* frame = registry.find_resource<FrameTime>();
        if (!frame)
            frame = &registry.set_resource<FrameTime>();

        sim->dt = fixed_dt;
        accumulator += real_dt;

        uint32_t ticks = 0;
        while (accumulator >= fixed_dt && ticks < max_ticks_per_frame)
        {
            ++sim->tick;
            sim->time = double(sim->tick) * fixed_dt;
            run_phase(registry, Phase::FixedPre);
            run_phase(registry, Phase::Fixed);
            run_phase(registry, Phase::FixedPost);
            accumulator -= fixed_dt;
            ++ticks;
        }
        if (accumulator >= fixed_dt)
            accumulator = std::fmod(accumulator, fixed_dt);

        ++frame->frame;
        frame->dt = real_dt;
        frame->time += real_dt;
        frame->alpha = accumulator / fixed_dt;
    }
}
