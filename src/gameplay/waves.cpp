module;

#include <json/value.h>

module gameplay;

import :waves;
import :health;
import :player;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import physics;
import cvar;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    float g_timer = 0.0f;
    int g_spawn_now = 0;
    bool g_clear = false;
    std::mt19937 g_rng{ 0x5eedu };

    cvar::Command cmd_spawn("waves.spawn", "Spawns wave enemies around the player at once (also with game.waves.enabled off)",
        [] (cvar::Args args)
        {
            int count = 1;
            if (!args.empty())
                std::from_chars(args[0].data(), args[0].data() + args[0].size(), count);
            g_spawn_now += std::clamp(count, 1, 64);
        },
        "[count]");

    cvar::Command cmd_clear("waves.clear", "Removes every wave enemy",
        [] (cvar::Args) { g_clear = true; });

    // a free spot on the ground in the ring around the player, facing the player
    std::optional<Transform> find_spawn_point(const phys::PhysicsScene& physics, const glm::vec3& player)
    {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const float r_min = std::max(cv_waves_radius_min.get(), 0.5f);
        const float r_max = std::max(cv_waves_radius_max.get(), r_min + 0.1f);
        phys::QueryFilter filter;
        filter.categories = phys::Category::static_world;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            const float angle = unit(g_rng) * 6.28318531f;
            // uniform over the ring's area
            const float distance = std::sqrt(glm::mix(r_min * r_min, r_max * r_max, unit(g_rng)));
            const glm::vec3 above = player + glm::vec3(std::cos(angle) * distance, 2.5f, std::sin(angle) * distance);
            // the floor near the player's height (not the roof above, not a pit far below)
            std::optional<phys::Hit> ground = physics.raycast(above, glm::vec3(0.0f, -1.0f, 0.0f), 6.0f, filter);
            if (!ground || ground->normal.y < 0.7f || std::abs(ground->position.y - player.y) > 3.0f)
                continue;
            // standing room: nothing over the spot up to 1.8 m
            if (physics.raycast(ground->position + glm::vec3(0.0f, 0.05f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1.8f, filter))
                continue;
            const glm::vec3 to_player = player - ground->position;
            const float yaw = std::atan2(to_player.x, to_player.z);
            Transform placement;
            placement.position = ground->position;
            placement.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            return placement;
        }
        return std::nullopt;
    }

    // after the blows of the tick (the dead are not counted), before the physics step
    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::after<DamageResolution>, =ecs::before<scene::PhysicsStep>]]
    void spawn_waves(ecs::Query<const WorldTransform, ecs::With<Player>> players, ecs::Query<const WaveEnemy, ecs::Without<Dead>> alive,
        ecs::Query<const WaveEnemy> all, ecs::Res<phys::PhysicsScene> physics, ecs::Res<ecs::SimTime> time,
        ecs::EventWriter<SpawnPrefabRequest> spawns, ecs::EventWriter<DespawnRequest> despawns)
    {
        if (std::exchange(g_clear, false))
            all.each([&] (ecs::Entity e, const WaveEnemy&) { despawns.send({ e }); });

        std::optional<glm::vec3> player;
        players.each([&] (const WorldTransform& transform) {
            if (!player)
                player = transform.value.position.glm();
        });
        if (!player)
            return;

        int wanted = std::exchange(g_spawn_now, 0);
        g_timer -= float(time->dt);
        if (cv_waves_enabled.get() && g_timer <= 0.0f && int(alive.count()) < cv_waves_alive.get())
        {
            ++wanted;
            g_timer = cv_waves_interval.get();
        }

        for (int i = 0; i < wanted; ++i)
        {
            std::optional<Transform> placement = find_spawn_point(*physics, *player);
            if (!placement)
                continue;
            spawns.send({
                .prefab = cv_waves_prefab.get(),
                .placement = *placement,
                .on_spawned = [] (ecs::Registry& registry, ecs::Entity e) { registry.add<WaveEnemy>(e); },
            });
        }
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(WaveEnemy)
}
