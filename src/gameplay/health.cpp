module;

#include <json/value.h>

module gameplay;

import :health;
import :player;

import std.compat;
import glm;
import reflect;
import rhobject;
import ecs;
import framework;
import rhcomponents;

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

    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::in_set<HealthRegeneration>, =ecs::after<DamageResolution>,
        =ecs::before<scene::PhysicsStep>]]
    void regenerate_health(ecs::EventReader<DamageTakenEvent> taken, ecs::Query<Health, HealthRegen> regens,
        ecs::Query<const Dead> dead, ecs::Res<ecs::SimTime> time)
    {
        // any blow that took health restarts the wait
        for (const DamageTakenEvent& event : taken.read())
        {
            if (event.health_after >= event.health_before)
                continue;
            if (HealthRegen* regen = regens.get<HealthRegen>(event.entity))
                regen->since_damage = 0.0f;
        }

        const float dt = float(time->dt);
        regens.each([&] (ecs::Entity e, Health& health, HealthRegen& regen) {
            regen.since_damage += dt;
            if (dead.get(e) || health.invulnerable)
                return;
            const float cap = health.max * std::clamp(regen.limit, 0.0f, 1.0f);
            const float healing = regen.since_damage - std::max(regen.delay, 0.0f);
            if (healing <= 0.0f || health.current >= cap)
                return;
            const float speed = std::clamp(healing / std::max(regen.ramp, 1e-3f), 0.0f, 1.0f);
            health.current = std::min(health.current + std::max(regen.rate, 0.0f) * speed * dt, cap);
        });
    }

    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::after<DamageResolution>, =ecs::after<HealthRegeneration>]]
    void despawn_dead(ecs::Query<Dead, const Health> dead, ecs::Res<ecs::SimTime> time, ecs::EventWriter<DespawnRequest> despawn)
    {
        dead.each([&] (ecs::Entity e, Dead& state, const Health& health) {
            const bool due_before = health.despawn_delay >= 0.0f && state.time >= health.despawn_delay;
            state.time += float(time->dt);
            // once (the World destroys it with its children at the next sync point)
            if (!due_before && health.despawn_delay >= 0.0f && state.time >= health.despawn_delay)
                despawn.send({ e });
        });
    }

    // a new blow restarts the flash (the stronger color wins while the old one is still bright) and the body JPEG
    [[=ecs::system<ecs::Phase::Update>]]
    void start_hit_flashes(ecs::EventReader<DamageTakenEvent> taken, ecs::Query<HitFlash> flashes, ecs::Query<const Player> players,
        ecs::Commands& commands)
    {
        for (const DamageTakenEvent& event : taken.read())
        {
            const DamageEvent& blow = event.damage;
            const bool flash_color = blow.flash != glm::vec3(0.0f) && blow.flash_time > 0.0f;
            // not over the player's own view
            const float jpeg = !blow.jpeg_preset.empty() && !players.get(event.entity) ? std::max(blow.body_jpeg_time, 0.0f) : 0.0f;
            if (!flash_color && jpeg <= 0.0f)
                continue;
            if (HitFlash* flash = flashes.get(event.entity))
            {
                const float left = 1.0f - std::clamp(flash->time / std::max(flash->duration, 1e-3f), 0.0f, 1.0f);
                flash->color = glm::max(flash->color * left * left, flash_color ? blow.flash : glm::vec3(0.0f));
                flash->duration = flash_color ? blow.flash_time : flash->duration;
                flash->jpeg_duration = std::max(jpeg, flash->jpeg_duration - flash->time);
                flash->time = 0.0f;
                continue;
            }
            commands.add(event.entity, HitFlash{
                .color = flash_color ? blow.flash : glm::vec3(0.0f),
                .duration = flash_color ? blow.flash_time : 0.0f,
                .jpeg_duration = jpeg,
            });
        }
    }

    // the flash of an entity goes to its MeshRenderers and those of its descendants (weapon models, child meshes)
    [[=ecs::system<ecs::Phase::Late>, =ecs::in_set<HitFlashes>, =ecs::before<RenderSync>, =ecs::before<MeshColliderSync>]]
    void apply_hit_flashes(ecs::Query<HitFlash> flashes, ecs::Query<MeshRenderer> renderers, ecs::Query<const ChildOf> parents,
        ecs::Res<ecs::FrameTime> time, ecs::Commands& commands)
    {
        if (flashes.empty())
            return;
        std::unordered_multimap<uint64_t, ecs::Entity> children;
        parents.each([&] (ecs::Entity e, const ChildOf& child_of) { children.emplace(child_of.parent.bits(), e); });

        flashes.each([&] (ecs::Entity e, HitFlash& flash) {
            flash.time += float(time->dt);
            const float left = 1.0f - std::clamp(flash.time / std::max(flash.duration, 1e-3f), 0.0f, 1.0f);
            // the body JPEG: full for the first half, then fading (3 levels in the g-buffer: steps)
            const float jpeg_left = flash.jpeg_duration > 0.0f
                ? std::clamp(2.0f * (1.0f - flash.time / flash.jpeg_duration), 0.0f, 1.0f) : 0.0f;
            const glm::vec4 effect(flash.color * left * left, jpeg_left);

            std::vector<ecs::Entity> stack{ e };
            for (int guard = 0; !stack.empty() && guard < 4096; ++guard)
            {
                const ecs::Entity node = stack.back();
                stack.pop_back();
                if (MeshRenderer* renderer = renderers.get(node))
                    renderer->effect = effect;
                auto [first, last] = children.equal_range(node.bits());
                for (auto it = first; it != last; ++it)
                    stack.push_back(it->second);
            }
            if (left <= 0.0f && jpeg_left <= 0.0f)
                commands.remove<HitFlash>(e);
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Health, HealthRegen, Dead, HitFlash)
}
