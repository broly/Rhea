module;

#include <json/value.h>

export module gameplay:waves;

import std.compat;
import reflect;
import rhobject;
import cvar;
import framework;

// Waves of enemies for the M0 prototype (G2): keeps game.waves.alive enemies (game.waves.prefab, the jackal)
// around the player, one every game.waves.interval seconds, on the ground in a ring of radius_min..radius_max m
// (a ray down near the player's height; they find their way on the navigation mesh). Spawned through
// SpawnPrefabRequest, despawned by their Health (despawn_delay).
//
// Console: waves.spawn [n] (at once, also when off), waves.clear
export
{
    struct [[=scene::runtime_only]] WaveEnemy {};

    cvar::Var<bool> cv_waves_enabled("game.waves.enabled", false, "Keeps enemies (dummies) spawning around the player");
    cvar::Var<std::string> cv_waves_prefab("game.waves.prefab", "prefabs/jackal.json", "Prefab of the wave enemies");
    cvar::Var<int> cv_waves_alive("game.waves.alive", 6, "Enemies alive at once",
        { .has_range = true, .min = 1.0f, .max = 64.0f });
    cvar::Var<float> cv_waves_interval("game.waves.interval", 1.5f, "Seconds between two spawns",
        { .has_range = true, .min = 0.05f, .max = 30.0f });
    cvar::Var<float> cv_waves_radius_min("game.waves.radius_min", 5.0f, "Nearest spawn distance from the player, m",
        { .has_range = true, .min = 1.0f, .max = 100.0f });
    cvar::Var<float> cv_waves_radius_max("game.waves.radius_max", 16.0f, "Farthest spawn distance from the player, m",
        { .has_range = true, .min = 1.0f, .max = 200.0f });
}
