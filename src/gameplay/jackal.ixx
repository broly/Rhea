module;

#include <json/value.h>

export module gameplay:jackal;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import assets;
import physics;
import animation;
import framework;

// The jackal (M0 enemy): a quadruped that runs at the player and stops in front of it.
//
//   Jackal           (JSON) movement tuning + simulation state. FixedPost (set JackalSimulation, before the weapons):
//                    steering towards the player with acceleration, braking distance, a turn rate that drops with the
//                    speed, slowing down for sharp turns, separation from the other jackals, sliding along walls,
//                    paws on the ground (raycasts), body pitched to the slope; a kinematic hitbox follows it
//   interpolation    Late, before the transforms are propagated: Transform between the last two ticks, with the
//                    pitch and the lean into turns (centripetal: atan(v * turn rate / g))
//   JackalAnimation  (runtime) idle / locomotion / death on its anim::Animator. Gaits walk, trot, run, sprint from
//                    animations/jackal/locomotion.json (tools/creatures/import_jackal.py: clips in place, the
//                    ground speed of each measured from the paws in contact): adjacent gaits blend by speed in one
//                    sync group (left hind paw down), playback rate = speed / speed of the blend, so the paws on the
//                    ground do not slide; turning bends the spine into the turn and swings the tail out
//
//     "Jackal": { "run_speed": 6.0, "stop_distance": 1.3 }
//
// cvars game.jackal.chase (off: they stay where they are), game.jackal.clips; state lines in Windows > Animation
export
{
    struct JackalSimulation {};

    struct Jackal
    {
        // ---- tuning
        [[=rh::edit]] bool chase = true;                                     // runs at the player when it is in reach
        [[=rh::edit, =rh::speed<0.5f>]] float aggro_radius = 40.0f;          // m
        [[=rh::edit, =rh::speed<0.05f>]] float stop_distance = 1.3f;         // m from the player
        [[=rh::edit, =rh::speed<0.05f>]] float run_speed = 6.0f;             // m/s, chasing
        [[=rh::edit, =rh::speed<0.05f>]] float acceleration = 7.0f;          // m/s2
        [[=rh::edit, =rh::speed<0.05f>]] float deceleration = 10.0f;         // m/s2, also plans the stop
        [[=rh::edit, =rh::speed<1.f>]] float turn_rate = 360.0f;             // deg/s standing
        [[=rh::edit, =rh::speed<1.f>]] float turn_rate_at_speed = 160.0f;    // deg/s at run_speed (turn radius)
        [[=rh::edit, =rh::speed<10.f>]] float turn_acceleration = 1440.0f;   // deg/s2
        [[=rh::edit, =rh::speed<0.01f>]] float radius = 0.3f;                // m, separation and walls
        [[=rh::edit, =rh::speed<0.01f>]] float lean = 0.7f;                  // share of the physical lean into turns
        [[=rh::edit, =rh::speed<0.01f>]] vec3 hitbox_half_extents{ 0.14f, 0.26f, 0.5f };   // m, model space (+z forward)
        [[=rh::edit, =rh::speed<0.01f>]] vec3 hitbox_center{ 0.0f, 0.36f, -0.02f };

        // ---- simulation (FixedPost)
        [[=rh::transient]] bool initialized = false;
        [[=rh::transient]] glm::vec3 position{ 0.0f };          // feet, on the ground
        [[=rh::transient]] glm::vec3 previous_position{ 0.0f };
        [[=rh::transient]] float heading = 0.0f;                 // rad, yaw of +z (Rhea: rotation = angleAxis(heading, y))
        [[=rh::transient]] float previous_heading = 0.0f;
        [[=rh::transient]] float pitch = 0.0f;                   // rad, slope under the body (nose up > 0)
        [[=rh::transient]] float previous_pitch = 0.0f;
        [[=rh::transient]] float lean_angle = 0.0f;              // rad, roll into the turn
        [[=rh::transient]] float previous_lean = 0.0f;
        [[=rh::edit, =rh::read_only, =rh::transient]] float speed = 0.0f;          // m/s along the heading
        [[=rh::edit, =rh::read_only, =rh::transient]] float yaw_rate = 0.0f;       // rad/s, + = turning left (heading grows)
        [[=rh::edit, =rh::read_only, =rh::transient]] float desired_speed = 0.0f;
        [[=rh::edit, =rh::read_only, =rh::transient]] float target_distance = 0.0f;
        [[=rh::transient]] phys::BodyId hitbox;
        [[=rh::transient]] phys::Shape hitbox_shape;
    };

    // Clips of the jackal and their measured ground speeds (animations/jackal/locomotion.json)
    struct JackalClips
    {
        struct Gait
        {
            AnimationClipHandle clip;
            float speed = 0.0f;       // m/s at playback rate 1
            float stride = 0.0f;      // m per cycle
        };
        AnimationClipHandle idle;
        AnimationClipHandle death;
        std::vector<Gait> gaits;      // by ascending speed

        bool load(const std::string& json_path);
    };

    struct [[=scene::runtime_only]] JackalAnimation
    {
        std::shared_ptr<const JackalClips> clips;

        enum class State : uint8_t { alive, dead };
        anim::StateMachine<State> states;
        anim::SequencePlayer idle;
        std::array<anim::SequencePlayer, 8> gait_players;
        anim::SequencePlayer death;
        anim::ScaleBiasClamp moving;     // 0 idle .. 1 locomotion, smoothed
        float animation_speed = 0.0f;    // smoothed speed the gaits are picked by
        float bend = 0.0f;               // smoothed spine bend, rad
        float bend_velocity = 0.0f;

        // bones of the procedural bend (-1: missing)
        std::array<int32_t, 6> spine{ -1, -1, -1, -1, -1, -1 };
        std::array<int32_t, 5> tail{ -1, -1, -1, -1, -1 };
        glm::vec3 up_model{ 0.0f, 1.0f, 0.0f };   // world up in the rig's model space

        [[=rh::edit, =rh::read_only]] float rate = 1.0f;
        [[=rh::edit, =rh::read_only]] float gait_position = 0.0f;   // index into the gaits, fraction = blend
    };
}
