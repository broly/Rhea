module;

#include <json/value.h>

export module ai:perception;

import std.compat;
import glm;
import reflect;
import rhobject;
import ecs;

// Perception (AI3): what an agent knows about the others. Not a list of who is where - a memory that fills up
// and fades like an animal's:
//
//   Perceivable      (JSON) on whoever can be seen or heard: the player, the companion, the creatures
//                    (team: hostile = another non neutral team). Its velocity is measured per frame.
//   Perception       (JSON) on whoever perceives (the creatures, the companion); its team is that of its own
//                    Perceivable. Fills `known` in FixedPre (set ai::PerceptionUpdate, before the brains):
//     sight          hostiles within sight_range inside the field of view, or within near_range all around,
//                    with a clear ray from the eyes (Jolt, level geometry), checked every
//                    ai.perception.sight_interval s per perceiver (spread over the ticks). Seeing raises the
//                    awareness: faster close by, in the middle of the view, and when the other runs
//     hearing        footsteps (speed x Perceivable::footsteps m around a running hostile) and Stimulus noises
//     blows / pack   Stimulus damage (the attacker is known at once) and shared (a howl: the pack learns
//                    where the target is)
//   KnownTarget      awareness 0..1 (1: confirmed - it knows a hostile is there), last position and velocity,
//                    visible now or not. Unsensed it fades (awareness_decay per s); at 0 it is forgotten.
//
// Brains read Perception::known (best(), find()). cvar ai.debug.perception draws the views and the memories.
export namespace ai
{
    struct PerceptionUpdate {};   // FixedPre: stimuli + senses -> Perception::known

    enum class Team : uint8_t { neutral, player, jackals, wildlife };

    // a and b fight each other
    constexpr bool hostile(Team a, Team b)
    {
        return a != b && a != Team::neutral && b != Team::neutral;
    }

    struct Perceivable
    {
        [[=rh::edit]] Team team = Team::neutral;
        [[=rh::edit]] bool enabled = true;                                 // off: nobody senses it (the dead)
        [[=rh::edit, =rh::speed<0.01f>]] float center_height = 1.0f;       // m above the feet: where the eyes aim
        [[=rh::edit, =rh::speed<0.01f>]] float footsteps = 1.5f;           // heard within speed x this m
        [[=rh::edit, =rh::speed<0.01f>]] float visibility = 1.0f;          // how fast it is noticed (crouching < 1)

        // ---- measured per frame (Late)
        [[=rh::transient]] glm::vec3 position{ 0.0f };   // feet
        [[=rh::transient]] glm::vec3 velocity{ 0.0f };
        [[=rh::transient]] bool measured = false;
    };

    enum class Sense : uint8_t { sight, hearing, damage, shared };

    struct KnownTarget
    {
        ecs::Entity entity;
        glm::vec3 last_position{ 0.0f };   // feet, where it was last sensed
        glm::vec3 last_velocity{ 0.0f };
        float awareness = 0.0f;            // 0..1
        bool confirmed = false;            // awareness reached 1; stays until forgotten
        bool visible = false;              // in sight at the last look
        Sense sense = Sense::sight;        // how it was last sensed
        double last_sensed = 0.0;          // SimTime
        float distance = 0.0f;             // from the perceiver, at the last update (to the last position)
    };

    struct Perception
    {
        [[=rh::edit, =rh::speed<0.1f>]] float sight_range = 30.0f;        // m
        [[=rh::edit, =rh::speed<1.f>]] float field_of_view = 200.0f;      // degrees, the whole angle
        [[=rh::edit, =rh::speed<0.05f>]] float near_range = 3.0f;         // m, noticed all around (and at once)
        [[=rh::edit, =rh::speed<0.01f>]] float eye_height = 0.5f;         // m above the feet
        [[=rh::edit, =rh::speed<0.01f>]] float awareness_rise = 1.0f;     // per s, at mid range in the middle of the view
        [[=rh::edit, =rh::speed<0.01f>]] float awareness_decay = 0.1f;    // per s while unsensed (1 / memory in s)
        [[=rh::edit, =rh::speed<0.01f>]] float hearing = 1.0f;            // scales the radii of what it hears

        [[=rh::transient]] std::vector<KnownTarget> known;
        [[=rh::transient]] double next_look = -1.0;     // SimTime of the next sight check (< 0: not started)
        [[=rh::transient]] glm::vec3 eyes{ 0.0f };
        [[=rh::transient]] glm::vec3 forward{ 0.0f, 0.0f, 1.0f };

        const KnownTarget* find(ecs::Entity e) const
        {
            for (const KnownTarget& k : known)
                if (k.entity == e)
                    return &k;
            return nullptr;
        }

        KnownTarget* find(ecs::Entity e)
        {
            for (KnownTarget& k : known)
                if (k.entity == e)
                    return &k;
            return nullptr;
        }

        // the one it is surest of (the nearest of equals) with at least min_awareness; confirmed ones only by default
        const KnownTarget* best(float min_awareness = 1.0f) const
        {
            const KnownTarget* result = nullptr;
            for (const KnownTarget& k : known)
            {
                const float a = k.confirmed ? 1.0f : k.awareness;
                if (a < min_awareness)
                    continue;
                const float best_a = result ? (result->confirmed ? 1.0f : result->awareness) : -1.0f;
                if (!result || a > best_a + 1e-3f || (std::abs(a - best_a) <= 1e-3f && k.distance < result->distance))
                    result = &k;
            }
            return result;
        }

        void forget(ecs::Entity e)
        {
            std::erase_if(known, [e] (const KnownTarget& k) { return k.entity == e; });
        }
    };

    // Something sensed besides sight, sent by game code (gameplay turns shots and blows into these):
    //   noise    every perceiver within radius x its hearing of origin learns of source if it is hostile to it,
    //            the surer the closer (awareness at origin, 0 at the edge)
    //   damage   receiver learns of source at once (it was hit by it)
    //   shared   the perceivers of `team` within radius of origin learn where source is (a howl)
    struct Stimulus
    {
        enum class Kind : uint8_t { noise, damage, shared };

        Kind kind = Kind::noise;
        ecs::Entity source;             // who is sensed (the shooter, the attacker, the shared target)
        glm::vec3 position{ 0.0f };     // where source is (its feet)
        glm::vec3 origin{ 0.0f };       // where it is sensed from (noise, shared: the radius is around it)
        float radius = 0.0f;            // m
        float awareness = 1.0f;
        ecs::Entity receiver;           // damage
        Team team = Team::neutral;      // shared
    };
}
