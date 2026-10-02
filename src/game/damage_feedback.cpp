module game;

import :damage_feedback;

import std.compat;
import glm;
import rhmath;
import ecs;
import framework;
import cvar;
import gameplay;
import globals;
import engine;
import :screen_jpeg;
import :jpeg_hits;
import :hud;

#include "ecs/ecs_macros.h"

namespace
{
    std::optional<damage_feedback::LookRequest> g_look_request;

    float parse_float(cvar::Args args, size_t index, float fallback)
    {
        float value = fallback;
        if (index < args.size())
            std::from_chars(args[index].data(), args[index].data() + args[index].size(), value);
        return value;
    }

    cvar::Command cmd_damage_player("damage.player", "Damages the player (full screen JPEG)",
        [] (cvar::Args args)
        {
            ecs::Registry& registry = RhGlobals::engine->world->registry;
            ecs::Entity player;
            ecs::Query<const Health, const Player>(registry).each([&] (ecs::Entity e, const Health&, const Player&) {
                if (!player)
                    player = e;
            });
            if (!player)
            {
                cvar::print("no player with a Health (add \"Health\" and \"Player\" to the player entity)", cvar::Output::error);
                return;
            }
            registry.events<DamageEvent>().send({
                .target = player,
                .amount = parse_float(args, 0, 10.0f),
                .jpeg_preset = args.size() > 1 ? args[1] : std::string(),
            });
        },
        "<amount> [jpeg preset]");

    cvar::Command cmd_damage_look("damage.look", "Damages what is in the middle of the screen (a JPEG spot of the preset)",
        [] (cvar::Args args)
        {
            g_look_request = damage_feedback::LookRequest{
                .amount = parse_float(args, 0, 10.0f),
                .preset = args.size() > 1 ? args[1] : std::string("default"),
            };
        },
        "<amount> [jpeg preset]");

    [[=ecs::system<ecs::Phase::Update>]]
    void jpeg_damage_feedback(ecs::EventReader<DamageEvent> damage, ecs::EventReader<DamageTakenEvent> taken,
        ecs::EventReader<DeathEvent> deaths, ecs::Query<const Health, const Player> players,
        ecs::Query<const Player> player_tags, ecs::Query<const WorldTransform> transforms, ecs::Res<ecs::FrameTime> time)
    {
        float health = 1.0f;
        bool found = false;
        players.each([&] (const Health& h, const Player&) {
            if (!found)
                health = h.fraction();
            found = true;
        });
        screen_jpeg::set_health(health);
        hud::set_player_health(health, found);
        hud::tick(float(time->dt));

        // the player's blows: a pulse of the full screen JPEG and red edges; a quarter of the health at once is the
        // strongest one
        for (const DamageTakenEvent& event : taken.read())
        {
            if (!player_tags.get(event.entity) || event.max <= 0.0f)
                continue;
            const float lost = (event.health_before - event.health_after) / event.max;
            screen_jpeg::hit(std::clamp(0.35f + lost * 2.6f, 0.35f, 1.0f));
            hud::player_hit();
        }

        // spots where the blows land: on the world and the creatures, not on the player's own view
        for (const DamageEvent& event : damage.read())
        {
            if (event.jpeg_preset.empty() || player_tags.get(event.target))
                continue;
            jpeg_hits::add(event.point, event.jpeg_preset);
        }

        for (const DeathEvent& event : deaths.read())
        {
            if (event.death_jpeg.empty() || player_tags.get(event.entity))
                continue;
            const WorldTransform* transform = transforms.get(event.entity);
            const glm::vec3 point = transform ? transform->value.position.glm() : event.killing_blow.point;
            jpeg_hits::add(point, event.death_jpeg);
        }
    }

    ECS_REGISTER()
}

std::optional<damage_feedback::LookRequest> damage_feedback::take_look_request()
{
    return std::exchange(g_look_request, std::nullopt);
}
