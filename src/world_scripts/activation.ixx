module;

#include <json/value.h>

export module activation;

import std.compat;
import glm;
import reflect;
import rhmath;

// Activatable objects: an Activator on the root of the object is switched on by a character coming close, and
// plays its timeline forwards (0 -> duration); switched off, it plays it back. ActivatedMotions on the entities of
// the object (the Activator's own or its descendants') move them along that timeline: a bolt slides into the wall,
// then the door swings open; leaving, the door closes and the bolt slides back.
//
//     "Activator": { "radius": 2.2, "duration": 2.0 }                               on the root, at the doorway
//     "ActivatedMotion": { "translation": { "x": 0, "y": 0, "z": -1.75 }, "duration": 0.6 }               bolt
//     "ActivatedMotion": { "angle": 100, "start": 0.5, "duration": 1.5, "away": true }   leaf, origin on the hinge
//
// Moving parts with collision: MeshCollider with "kinematic": true (the body follows the entity).
export
{
    // System set of the activator updates: ActivatedMotions play after it
    struct Activation {};

    struct Activator
    {
        // switched on when the feet of a character come within `radius` of the entity's origin...
        [[=rh::edit, =rh::speed<0.05f>]] float radius = 2.0f;
        // ...and off once none has been within `release_radius` for `close_delay` seconds
        [[=rh::edit, =rh::speed<0.05f>]] float release_radius = 3.0f;
        [[=rh::edit, =rh::speed<0.05f>]] float close_delay = 1.0f;
        // never switched off (a gate that stays open)
        [[=rh::edit]] bool stays_on = false;
        // length of the timeline, s: the ActivatedMotions of the object end within it
        [[=rh::edit, =rh::speed<0.05f>]] float duration = 1.0f;

        [[=rh::edit, =rh::read_only, =rh::transient]] bool active = false;
        [[=rh::edit, =rh::read_only, =rh::transient]] float time = 0.0f;
        // since no character is within release_radius
        [[=rh::transient]] float away_time = 0.0f;
        // switched on at the start of the timeline (time 0) so many times, the last time by a character here (world)
        [[=rh::transient]] uint32_t activations = 0;
        [[=rh::transient]] glm::vec3 activated_from{ 0.0f };
    };

    // Moves the entity along the timeline of the nearest Activator (its own or an ancestor's), from its Transform
    // at load (rest) to the end pose: moved by `translation` (parent space), turned by `angle` degrees about
    // `axis` (local) through the entity's origin. Eased in and out.
    struct ActivatedMotion
    {
        [[=rh::edit, =rh::speed<0.01f>]] vec3 translation{ 0.0f, 0.0f, 0.0f };
        [[=rh::edit, =rh::speed<0.01f>]] vec3 axis{ 0.0f, 1.0f, 0.0f };
        [[=rh::edit, =rh::speed<0.5f>]] float angle = 0.0f;
        // where on the timeline, s
        [[=rh::edit, =rh::speed<0.01f>]] float start = 0.0f;
        [[=rh::edit, =rh::speed<0.01f>]] float duration = 1.0f;
        // a door: turns away from the character that switched the Activator on (the sign of `angle` flips when
        // the turn would bring the middle of the entity's mesh towards it)
        [[=rh::edit]] bool away = false;

        // 0: rest, 1: end pose
        [[=rh::edit, =rh::read_only, =rh::transient]] float progress = 0.0f;
        [[=rh::transient]] std::optional<Transform> rest;
        [[=rh::transient]] bool applied = false;
        [[=rh::transient]] float sign = 1.0f;
        [[=rh::transient]] uint32_t activation = 0;     // Activator::activations the sign was chosen for
    };
}
