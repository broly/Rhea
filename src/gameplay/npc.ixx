module;

#include <json/value.h>

export module gameplay:npc;

import std.compat;
import glm;
import reflect;
import rhobject;
import ecs;

// Non-player characters on the locomotion rules of the player (ALS: gaits, turning in place, stairs on the Jolt
// capsule), walking the navigation mesh: the companion (AI6). It follows the player and shoots the creatures.
//
//   Npc              (JSON) on an entity with a SkinnedMesh of an ALS skeleton and a NavAgent: on spawn it becomes
//                    a locomotion character (capsule, animation) like the player
//   plan             FixedPre, before nav::NavAgentUpdate: the agent's body = the capsule; follows the player from
//                    catch_up_distance on until follow_distance, the gait by the distance (walk / run / sprint) and
//                    the crowd's speed with it
//   drive            FixedPre, after nav::NavAgentUpdate, before the locomotion input: LocomotionInput from the
//                    crowd's desired velocity (velocity direction rotation); standing it faces the player (view
//                    direction rotation: turns in place)
//
//   NpcCombat        (JSON, companion.cpp) FixedPre, after the perception: picks what to shoot among the hostiles it
//                    sees (ai::Perception) - a creature attacking the player first (it holds one of the player's
//                    attack tokens), then the ones near the player, then the ones near itself - waits its reaction
//                    time, aims (ALS aiming: it keeps following, strafing) and fires its WeaponHolder through a
//                    WeaponInput like the player's (weapons.json): the spread narrows over the first shots, bursts
//                    for automatic weapons, never through the player
//
//     "Npc": { "follow_player": true, "follow_distance": 2.5 },
//     "NavAgent": { "radius": 0.35, "height": 1.8 },
//     "NpcCombat": { "engage_range": 22 }, "WeaponHolder": { "current": "blaster", "infinite_ammo": true },
//     "Perception": {...}, "Perceivable": { "team": "player" }
//
// Off the navigation mesh (not built yet, no NavMeshBounds) it goes straight at the player.
// cvar game.companion.fire; the brain in Windows > AI.
export
{
    // FixedPre set of plan (before the crowd); other brains writing NavAgents order themselves after it
    struct NpcPlanning {};
    // FixedPre set of the NPCs' fighting (before drive: the aim turns the character)
    struct NpcCombatSet {};

    struct NpcCombat
    {
        [[=rh::edit, =rh::speed<0.1f>]] float engage_range = 22.0f;      // m from itself it shoots at
        [[=rh::edit, =rh::speed<0.1f>]] float protect_range = 10.0f;     // m from the player: shot even a bit beyond engage_range
        [[=rh::edit, =rh::speed<0.01f>]] float reaction_time = 0.35f;    // s from a new target to the first shot
        [[=rh::edit, =rh::speed<0.05f>]] float aim_error = 3.0f;         // degrees, the first shot at a target
        [[=rh::edit, =rh::speed<0.05f>]] float aim_error_min = 0.6f;     // degrees, after settle_shots shots
        [[=rh::edit]] int settle_shots = 3;
        [[=rh::edit, =rh::speed<0.01f>]] float cadence = 1.4f;           // x the weapon's cooldown between semi automatic shots
        [[=rh::edit, =rh::speed<0.01f>]] float burst_time = 0.5f;        // s, automatic weapons
        [[=rh::edit, =rh::speed<0.01f>]] float burst_pause = 0.45f;      // s
        [[=rh::edit, =rh::speed<0.01f>]] float eye_height = 1.45f;       // m above the feet: where its shots start

        [[=rh::transient]] ecs::Entity target;
        [[=rh::transient]] double target_since = 0.0;   // SimTime
        [[=rh::transient]] double next_shot = 0.0;
        [[=rh::transient]] double burst_end = 0.0;
        [[=rh::transient]] int shots = 0;               // at this target
        [[=rh::transient]] bool trigger_was_down = false;
        [[=rh::transient]] std::optional<float> aim_yaw;     // ALS yaw degrees, while it has a target
        [[=rh::transient]] float aim_pitch = 0.0f;           // degrees, up > 0
        [[=rh::transient]] std::minstd_rand rng;
        [[=rh::transient]] bool initialized = false;
        [[=rh::transient]] bool armed = false;               // the weapon models are spawned in its hand
    };

    struct Npc
    {
        [[=rh::edit, =rh::read_only]] std::string locomotion = "locomotion/als_character.json";   // settings asset

        [[=rh::edit]] bool follow_player = true;
        [[=rh::edit, =rh::speed<0.05f>]] float follow_distance = 2.5f;     // m, stops this close to the player
        [[=rh::edit, =rh::speed<0.05f>]] float catch_up_distance = 4.0f;   // m, sets off again when the player is this far
        [[=rh::edit, =rh::speed<0.1f>]] float run_distance = 7.0f;         // m, farther runs (walks closer)
        [[=rh::edit, =rh::speed<0.1f>]] float sprint_distance = 15.0f;     // m, farther sprints

        [[=rh::edit, =rh::read_only, =rh::transient]] bool following = false;
        [[=rh::edit, =rh::read_only, =rh::transient]] float player_distance = 0.0f;
        [[=rh::transient]] uint8_t gait = 1;                  // loco::Gait wanted: 0 walk, 1 run, 2 sprint
        [[=rh::transient]] std::optional<float> face_yaw;     // ALS yaw towards the player
        [[=rh::transient]] bool initialized = false;
    };
}
