module;

#include <json/value.h>

module gameplay;

import :health;

import std.compat;
import glm;
import reflect;
import rhobject;
import ecs;
import framework;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    // the entity itself or its nearest ancestor with a Health
    ecs::Entity find_health_owner(ecs::Entity e, const ecs::Query<Health>& healths, const ecs::Query<const ChildOf>& parents)
    {
        // a chain longer than this is a cycle
        for (int depth = 0; depth < 64 && e; ++depth)
        {
            if (healths.get(e))
                return e;
            const ChildOf* parent = parents.get(e);
            if (!parent)
                break;
            e = parent->parent;
        }
        return ecs::null_entity;
    }

    [[=ecs::on_add]]
    void init_health(ecs::Registry&, ecs::Entity, Health& health)
    {
        if (health.current < 0.0f)
            health.current = health.max;
    }

    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::in_set<DamageResolution>, =ecs::before<scene::PhysicsStep>]]
    void apply_damage(ecs::EventReader<DamageEvent> damage, ecs::Query<Health> healths, ecs::Query<const ChildOf> parents,
        ecs::Query<const Dead> dead, ecs::Commands& commands, ecs::EventWriter<DamageTakenEvent> taken,
        ecs::EventWriter<DeathEvent> deaths)
    {
        // several blows in one tick: only the first one past zero kills
        std::vector<ecs::Entity> killed;
        for (const DamageEvent& event : damage.read())
        {
            const ecs::Entity owner = find_health_owner(event.target, healths, parents);
            if (!owner || dead.get(owner) || std::ranges::contains(killed, owner))
                continue;
            Health& health = *healths.get(owner);
            const float before = health.current;
            if (!health.invulnerable)
                health.current = std::max(health.current - std::max(event.amount, 0.0f), 0.0f);
            taken.send({ .entity = owner, .damage = event, .health_before = before, .health_after = health.current,
                .max = health.max });

            if (health.current <= 0.0f && !health.invulnerable)
            {
                killed.push_back(owner);
                commands.add(owner, Dead{});
                deaths.send({ .entity = owner, .killing_blow = event, .death_jpeg = health.death_jpeg });
            }
        }
    }

    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::after<DamageResolution>]]
    void despawn_dead(ecs::Query<Dead, const Health> dead, ecs::Res<ecs::SimTime> time, ecs::Commands& commands)
    {
        dead.each([&] (ecs::Entity e, Dead& state, const Health& health) {
            state.time += float(time->dt);
            if (health.despawn_delay >= 0.0f && state.time >= health.despawn_delay)
                commands.destroy(e);
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Health, Dead)
}
