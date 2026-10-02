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
import ecs;
import ai;

// The jackal (enemy): a quadruped hunting in packs. Body and brain are apart:
//
//   Jackal           (JSON) the body: movement tuning + simulation state. FixedPost (set JackalSimulation, before the
//                    weapons): goes where the brain's JackalMove says - along the navigation mesh (its nav::NavAgent:
//                    the crowd's path and avoidance keep the pack apart) or straight (the leap) - with acceleration,
//                    braking distance (along the path), a turn rate that drops with the speed, slowing down for sharp
//                    turns, separation from the other jackals off the mesh, sliding along walls, paws on the ground
//                    (raycasts), body pitched to the slope; a kinematic hitbox follows it
//   interpolation    Late, before the transforms are propagated: Transform between the last two ticks, with the
//                    pitch and the lean into turns (centripetal: atan(v * turn rate / g))
//   JackalAnimation  (runtime) idle / locomotion / death on its anim::Animator. Gaits walk, trot, run, sprint from
//                    animations/jackal/locomotion.json (tools/creatures/import_jackal.py: clips in place, the
//                    ground speed of each measured from the paws in contact): adjacent gaits blend by speed in one
//                    sync group (left hind paw down), playback rate = speed / speed of the blend, so the paws on the
//                    ground do not slide; turning bends the spine into the turn and swings the tail out
//   JackalBrain      (JSON) the behaviour (jackal_brain.cpp, AI9), on ai:perception and ai:brain. FixedPre, after
//                    the perception, before the crowd (set JackalThink). Modes, decided 10 times per second:
//                      idle     rests, wanders its territory, keeps near the pack's leader
//                      alert    noticed something: watches it, then goes to look
//                      combat   the one that saw the target howls (the pack within earshot learns where it is);
//                               all of them take slots of a ring around the target - beside and behind it,
//                               the ring turning slowly - and attack from there: an attack token of the target
//                               (ai::AttackTokens: game.jackal.attackers at once on the player), a stare
//                               (telegraph, longer from outside the target's view), a leap straight at where
//                               the target will be (it commits: a side step dodges it), a bite, back to the ring.
//                               Feints without a token.
//                      flee     wounded (flee_health) or the pack broken (losses): runs home
//                      return   the target left the territory (leash): goes home and forgets it
//                    Speeds per mode: walking, trotting around the ring, running to catch up, sprinting only in
//                    the leap and briefly (stamina) after a target running away. The player runs 3.75, sprints 6.5.
//   JackalDen        (JSON) on a meadow: keeps `count` jackals (PackMember) around it; a dead one comes back after
//                    respawn_interval while no player is near; its territory and leash hold the pack; losses
//                    within morale_window break the pack for a while (they flee)
//
//     "Jackal": { "run_speed": 5.0 }, "JackalBrain": { "ring_radius": 5.5 }, "Perception": {...}, "Perceivable": {...}
//     "JackalDen": { "prefab": "prefabs/jackal.json", "count": 4, "territory_radius": 12, "leash_radius": 45 }
//
// cvars game.jackal.ai (off: they stand where they are), game.jackal.attackers, game.jackal.clips; state lines
// in Windows > Animation, the brains in Windows > AI, ai.debug.perception / game.jackal.debug draw them

// what the brain's actions work with (defined in jackal_brain.cpp)
struct JackalContext;

export
{
    struct JackalSimulation {};
    struct JackalThink {};

    // What the brain wants the body to do this tick (written before the simulation)
    struct JackalMove
    {
        std::optional<glm::vec3> destination;   // none: stands
        float speed = 0.0f;                     // m/s on the way
        float arrive_distance = 0.3f;           // m, stops this close (brakes in time, along the path)
        std::optional<glm::vec3> face;          // turns to look at it when standing (and arrived)
        bool direct = false;                    // straight there, not along the navigation mesh (the leap)
        float acceleration = 0.0f;              // m/s2, 0: Jackal::acceleration
        float deceleration = 0.0f;              // m/s2, 0: Jackal::deceleration
        float turn_scale = 1.0f;                // < 1: turns slower (commits to its line)
        // never closer than keep_away_distance to this point (its target's body: it bites, it does not run through)
        std::optional<glm::vec3> keep_away_from;
        float keep_away_distance = 0.0f;
    };

    struct Jackal
    {
        // ---- tuning
        [[=rh::edit, =rh::speed<0.05f>]] float run_speed = 5.0f;             // m/s where turn_rate_at_speed applies
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
        [[=rh::edit, =rh::read_only, =rh::transient]] float target_distance = 0.0f;   // to the brain's target
        [[=rh::transient]] JackalMove move;                      // from the brain, this tick
        [[=rh::transient]] phys::BodyId hitbox;
        [[=rh::transient]] phys::Shape hitbox_shape;
    };

    enum class JackalMode : uint8_t { idle, alert, combat, flee, return_home, dead };

    // ---- actions of the brain (ai::ActionRunner; jackal_brain.cpp). Each one sets Jackal::move every tick.

    // stands (looking at `face` if set) for a while
    struct JackalRest
    {
        static constexpr const char* name = "rest";
        float time_left = 3.0f;
        std::optional<glm::vec3> face;
        ai::ActionStatus tick(JackalContext& c, float dt);
    };

    // goes to a point; fails when it takes too long or there is no way
    struct JackalGoTo
    {
        static constexpr const char* name = "go to";
        glm::vec3 destination{ 0.0f };
        float speed = 1.3f;
        float arrive_distance = 0.5f;
        float time_left = 20.0f;
        ai::ActionStatus tick(JackalContext& c, float dt);
    };

    // tells the pack where the target is (ai::Stimulus shared), standing and facing it
    struct JackalHowl
    {
        static constexpr const char* name = "howl";
        float time_left = 1.0f;
        void start(JackalContext& c);
        ai::ActionStatus tick(JackalContext& c, float dt);
    };

    // holds its slot of the ring around the target (gets there first), facing the target
    struct JackalCircle
    {
        static constexpr const char* name = "circle";
        ai::ActionStatus tick(JackalContext& c, float dt);
    };

    // a short dash at the target and a stop - a threat without an attack token
    struct JackalFeint
    {
        static constexpr const char* name = "feint";
        float time = 0.0f;
        glm::vec3 point{ 0.0f };
        void start(JackalContext& c);
        ai::ActionStatus tick(JackalContext& c, float dt);
    };

    // telegraph (stands, stares) -> leap straight at where the target will be -> bite -> recover. Holds an attack
    // token of the target; committed once in the air.
    struct JackalLunge
    {
        static constexpr const char* name = "lunge";
        enum class Phase : uint8_t { telegraph, leap, recover };
        Phase phase = Phase::telegraph;
        float time = 0.0f;
        float telegraph = 0.45f;
        ecs::Entity target;
        glm::vec3 aim{ 0.0f };
        glm::vec3 direction{ 0.0f, 0.0f, 1.0f };
        bool bitten = false;
        void start(JackalContext& c);
        ai::ActionStatus tick(JackalContext& c, float dt);
        void stop(JackalContext& c);
        bool interruptible(const JackalContext& c) const;
    };

    using JackalActions = ai::ActionRunner<JackalContext, JackalRest, JackalGoTo, JackalHowl, JackalCircle, JackalFeint, JackalLunge>;

    struct JackalBrain
    {
        // ---- gaits, m/s (the player runs 3.75, sprints 6.5)
        [[=rh::edit, =rh::speed<0.05f>]] float walk_speed = 1.3f;
        [[=rh::edit, =rh::speed<0.05f>]] float trot_speed = 3.2f;          // around the ring, going to look
        [[=rh::edit, =rh::speed<0.05f>]] float run_speed = 5.0f;           // catching up, fleeing
        [[=rh::edit, =rh::speed<0.05f>]] float sprint_speed = 6.8f;        // after a target running away, while the stamina lasts
        [[=rh::edit, =rh::speed<0.05f>]] float stamina = 3.0f;             // s of sprinting
        [[=rh::edit, =rh::speed<0.05f>]] float stamina_recovery = 0.5f;    // s of sprint back per s
        [[=rh::edit, =rh::speed<0.05f>]] float lunge_speed = 9.0f;         // the leap

        // ---- fight
        [[=rh::edit, =rh::speed<0.05f>]] float ring_radius = 5.5f;         // m from the target
        [[=rh::edit, =rh::speed<0.1f>]] float orbit_speed = 10.0f;         // deg/s the ring turns around the target
        [[=rh::edit, =rh::speed<0.05f>]] float lunge_range = 7.0f;         // m, leaps from this close
        [[=rh::edit, =rh::speed<0.01f>]] float contact_distance = 0.85f;   // m between its feet and the target's: bodies touch, never closer
        [[=rh::edit, =rh::speed<0.01f>]] float telegraph = 0.45f;          // s staring before the leap, in the target's view
        [[=rh::edit, =rh::speed<0.01f>]] float telegraph_behind = 0.75f;   // s, from outside the target's view
        [[=rh::edit, =rh::speed<0.01f>]] float bite_range = 1.3f;          // m between its feet and the target's: the muzzle reaches
        [[=rh::edit, =rh::speed<0.5f>]] float bite_damage = 12.0f;
        [[=rh::edit]] std::string bite_effect = "prefabs/effects/jackal_bite.json";   // at the muzzle (empty: none)
        [[=rh::edit]] std::string bite_jpeg = "bite";                                 // JPEG spot preset at the muzzle (empty: none)
        [[=rh::edit, =rh::speed<0.05f>]] float lunge_cooldown_min = 2.5f;  // s between two leaps
        [[=rh::edit, =rh::speed<0.05f>]] float lunge_cooldown_max = 4.5f;
        [[=rh::edit, =rh::speed<0.01f>]] float feint_chance = 0.15f;       // per s, circling close without a token
        [[=rh::edit, =rh::speed<0.5f>]] float howl_radius = 60.0f;         // m the pack hears the howl

        // ---- will
        [[=rh::edit, =rh::speed<0.01f>]] float flee_health = 0.3f;         // share of the health it runs away below
        [[=rh::edit, =rh::speed<0.05f>]] float alert_time = 3.0f;          // s it watches something unclear before it goes to look
        [[=rh::edit, =rh::speed<0.5f>]] float territory_radius = 10.0f;    // m, without a den (a den sets its own)
        [[=rh::edit, =rh::speed<0.5f>]] float leash_radius = 60.0f;        // m from home it gives targets up (without a den)

        // ---- state
        [[=rh::edit, =rh::read_only, =rh::transient]] JackalMode mode = JackalMode::idle;
        [[=rh::transient]] ai::ThinkTimer think;
        [[=rh::transient]] JackalActions action;
        [[=rh::transient]] ecs::Entity target;
        [[=rh::transient]] std::optional<glm::vec3> home;        // the den, else where it appeared
        [[=rh::transient]] std::optional<glm::vec3> slot;        // its place on the ring (combat)
        [[=rh::transient]] int slot_index = -1;
        [[=rh::transient]] float stamina_left = 3.0f;
        [[=rh::transient]] double next_lunge = 0.0;              // SimTime
        [[=rh::transient]] double last_howl = -100.0;
        [[=rh::transient]] double mode_since = 0.0;
        [[=rh::transient]] std::minstd_rand rng;
        [[=rh::transient]] bool initialized = false;
    };

    // A pack's den on a meadow
    struct JackalDen
    {
        [[=rh::edit]] std::string prefab = "prefabs/jackal.json";
        [[=rh::edit]] int count = 4;                                        // jackals it keeps
        [[=rh::edit, =rh::speed<0.05f>]] float spawn_radius = 4.0f;         // m around the den
        [[=rh::edit, =rh::speed<0.5f>]] float territory_radius = 12.0f;     // m they wander
        [[=rh::edit, =rh::speed<0.5f>]] float leash_radius = 45.0f;         // m from the den: targets farther are given up
        [[=rh::edit, =rh::speed<0.5f>]] float respawn_interval = 45.0f;     // s per missing jackal
        [[=rh::edit, =rh::speed<0.5f>]] float respawn_distance = 50.0f;     // m: never while a player is closer
        [[=rh::edit, =rh::speed<0.01f>]] float morale_losses = 0.5f;        // share of `count` dying within the window breaks the pack
        [[=rh::edit, =rh::speed<0.5f>]] float morale_window = 20.0f;        // s
        [[=rh::edit, =rh::speed<0.5f>]] float broken_time = 12.0f;          // s the pack flees once broken

        [[=rh::edit, =rh::read_only, =rh::transient]] int alive = 0;
        [[=rh::transient]] bool populated = false;
        [[=rh::transient]] float respawn_timer = 0.0f;
        [[=rh::transient]] std::vector<double> deaths;          // SimTime, within the morale window
        [[=rh::transient]] double broken_until = -1.0;
        [[=rh::transient]] ecs::Entity leader;
        [[=rh::transient]] std::optional<glm::vec3> home;       // on the ground (first spawn)

        bool broken(double now) const { return now < broken_until; }
    };

    // A jackal of a den's pack
    struct [[=scene::runtime_only]] PackMember
    {
        ecs::Entity den;
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
