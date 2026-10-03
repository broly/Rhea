module;

#include <json/value.h>

module gameplay;

import :health;
import :weapons;

import std.compat;
import glm;
import rhmath;
import ecs;
import framework;
import ai;
import cvar;

#include "ecs/ecs_macros.h"

// The game's events as what the AI senses (ai::Stimulus), before the perception of the tick:
//   shots     a noise around the muzzle (game.ai.shot_noise m): hostiles within it learn of the shooter
//   blows     the one hit learns of who hit it; the attacker's side learns where the one hit is
//   the dead  are not perceived any more (Perceivable::enabled off)
namespace
{
    cvar::Var<float> cv_shot_noise("game.ai.shot_noise", 45.0f, "Shots are heard this far (m) by the creatures hostile to the shooter",
        { .has_range = true, .min = 0.0f, .max = 300.0f });
    cvar::Var<float> cv_hit_share("game.ai.hit_share", 40.0f,
        "The attacker's side within this many m of it learns where the one it hit is (the companion takes up the player's targets)",
        { .has_range = true, .min = 0.0f, .max = 300.0f });

    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::before<ai::PerceptionUpdate>]]
    void send_ai_stimuli(ecs::EventReader<WeaponFiredEvent> shots, ecs::EventReader<DamageTakenEvent> blows, ecs::Query<ai::Perceivable> perceivables,
        ecs::Query<const WorldTransform> transforms, ecs::Query<const Dead> dead, ecs::EventWriter<ai::Stimulus> stimuli)
    {
        auto feet_of = [&] (ecs::Entity e, const glm::vec3& otherwise) {
            if (const ai::Perceivable* p = perceivables.get(e); p && p->measured)
                return p->position;
            if (const WorldTransform* w = transforms.get(e))
                return w->value.position.glm();
            return otherwise;
        };

        for (const WeaponFiredEvent& shot : shots.read())
            if (shot.shooter)
                stimuli.send({
                    .kind = ai::Stimulus::Kind::noise,
                    .source = shot.shooter,
                    .position = feet_of(shot.shooter, shot.origin),
                    .origin = shot.origin,
                    .radius = cv_shot_noise.get(),
                    .awareness = 1.0f,
                });

        for (const DamageTakenEvent& blow : blows.read())
            if (blow.damage.source && blow.damage.source != blow.entity)
            {
                const glm::vec3 position = feet_of(blow.damage.source, blow.damage.point - blow.damage.direction * 2.0f);
                stimuli.send({
                    .kind = ai::Stimulus::Kind::damage,
                    .source = blow.damage.source,
                    .position = position,
                    .origin = position,
                    .receiver = blow.entity,
                });
                // the attacker's side learns where the one it hit is (the companion takes up the player's targets)
                const ai::Perceivable* attacker = perceivables.get(blow.damage.source);
                if (attacker && attacker->team != ai::Team::neutral && !dead.get(blow.entity))
                    stimuli.send({
                        .kind = ai::Stimulus::Kind::shared,
                        .source = blow.entity,
                        .position = feet_of(blow.entity, blow.damage.point),
                        .origin = position,
                        .radius = cv_hit_share.get(),
                        .awareness = 1.0f,
                        .team = attacker->team,
                    });
            }

        perceivables.each([&] (ecs::Entity e, ai::Perceivable& p) {
            if (dead.get(e))
                p.enabled = false;
        });
    }

    ECS_REGISTER()
}
