module;

#include <json/value.h>

export module gameplay:health;

import std.compat;
import glm;
import reflect;
import rhobject;
import ecs;
import framework;

// Health and damage (G3):
//   DamageEvent   anyone (weapons, explosions, debug commands) sends it, aimed at an entity
//   apply_damage  (FixedPost, set DamageResolution, before the physics step) takes it off the Health of the
//                 entity or of its nearest ancestor with one (a hit on a child mesh hurts the creature), adds
//                 Dead and sends DeathEvent when the health runs out
//   despawn_dead  (FixedPost, after DamageResolution) destroys the dead after Health::despawn_delay
// Weapons (FixedPost, set WeaponFire) send their DamageEvents before DamageResolution, after the characters
// moved (Fixed); reactions (effects, sounds, score) read the events in Update.
//
//     "Health": { "max": 50, "despawn_delay": 0.5, "death_jpeg": "death" }
export
{
    // System set of apply_damage
    struct DamageResolution {};

    enum class DamageType : uint8_t
    {
        Generic,
        Hitscan,        // instant ray (the squeezer)
        Projectile,     // blaster shots
        Explosion,
        Beam,           // continuous, per tick
    };

    struct Health
    {
        [[=rh::edit, =rh::speed<1.f>]] float max = 100.0f;
        // below 0 at spawn: max
        [[=rh::edit, =rh::speed<1.f>]] float current = -1.0f;
        // takes the damage events without losing health (still reported as hits)
        [[=rh::edit]] bool invulnerable = false;
        // seconds after death the entity is destroyed; below 0: it stays (a corpse)
        [[=rh::edit, =rh::speed<0.05f>]] float despawn_delay = -1.0f;
        // JPEG hit preset (assets/jpeg/presets.json) of the spot left where it dies; empty: none
        [[=rh::edit]] std::string death_jpeg = "death";

        float fraction() const { return max > 0.0f ? std::clamp(current / max, 0.0f, 1.0f) : 0.0f; }
    };

    // Added when the health runs out
    struct [[=scene::runtime_only]] Dead
    {
        [[=rh::edit, =rh::read_only]] float time = 0.0f;   // since death, s
    };

    struct DamageEvent
    {
        ecs::Entity target;             // the entity hit (or a child of the one with the Health)
        ecs::Entity source;             // who caused it (null: the world)
        float amount = 0.0f;
        DamageType type = DamageType::Generic;
        glm::vec3 point{ 0.0f };        // world, where it hit
        glm::vec3 direction{ 0.0f };    // world, normalized; 0 when there is none
        // JPEG hit preset of the spot at point (assets/jpeg/presets.json, section "hits"); empty: none
        std::string jpeg_preset;
    };

    // Sent by apply_damage after it applied a DamageEvent (with the entity that has the Health)
    struct DamageTakenEvent
    {
        ecs::Entity entity;
        DamageEvent damage;
        float health_before = 0.0f;
        float health_after = 0.0f;
        float max = 0.0f;
    };

    struct DeathEvent
    {
        ecs::Entity entity;             // the one with the Health
        DamageEvent killing_blow;
        std::string death_jpeg;         // Health::death_jpeg
    };
}
