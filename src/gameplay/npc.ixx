module;

#include <json/value.h>

export module gameplay:npc;

import std.compat;
import reflect;
import rhobject;

// Non-player characters on the locomotion rules of the player (ALS: gaits, turning in place, stairs on the Jolt
// capsule), walking the navigation mesh: the companion (AI6 gives it its behaviour; for now it follows the player).
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
//     "Npc": { "follow_player": true, "follow_distance": 2.5 },
//     "NavAgent": { "radius": 0.35, "height": 1.8 }
//
// Off the navigation mesh (not built yet, no NavMeshBounds) it goes straight at the player.
export
{
    // FixedPre set of plan (before the crowd); other brains writing NavAgents order themselves after it
    struct NpcPlanning {};

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
