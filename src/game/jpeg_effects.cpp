module game;

import :jpeg_hits;

import std.compat;
import glm;
import ecs;
import framework;
import gameplay;

#include "ecs/ecs_macros.h"

// Game events -> JPEG hits, besides the spots of the blows (damage_feedback.cpp):
//   JpegSpotEvent                 a spot where game rules say (a jackal's bite at its muzzle)
//   DamageEvent::body_jpeg_time   a creature hit by a weapon: its whole body under the blow's preset for that long
//                                 (gameplay HitFlash marks the body in the g-buffer, jpeg_hits::hold_body keeps the
//                                 preset's chain running and gives the bodies their mask channel)
namespace
{
    [[=ecs::system<ecs::Phase::Update>]]
    void jpeg_game_effects(ecs::EventReader<JpegSpotEvent> spots, ecs::EventReader<DamageTakenEvent> taken, ecs::Query<const Player> players)
    {
        for (const JpegSpotEvent& spot : spots.read())
            jpeg_hits::add(spot.position, spot.preset, spot.strength, spot.radius, spot.lifetime);

        for (const DamageTakenEvent& event : taken.read())
        {
            const DamageEvent& blow = event.damage;
            if (blow.body_jpeg_time <= 0.0f || blow.jpeg_preset.empty() || players.get(event.entity))
                continue;
            jpeg_hits::hold_body(blow.jpeg_preset, blow.body_jpeg_time);
        }
    }

    ECS_REGISTER()
}
