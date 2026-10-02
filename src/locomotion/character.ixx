module;

#include <json/value.h>

export module locomotion:character;

import std.compat;
import glm;
import rhmath;
import ecs;
import framework;
import reflect;
import physics;
import :types;
import :settings;

// A character moved by locomotion rules ported from ALS-Refactored (AAlsCharacter + its movement component),
// on a Jolt character capsule:
//
//   LocomotionInput     - command of one fixed tick (move axis, view, held / pressed actions); filled from the
//                         keyboard for LocomotionPlayer entities, by AI or scripts for others (FixedPre)
//   LocomotionCharacter - desired and actual states (gait, stance, rotation mode, locomotion mode / action),
//                         view, locomotion state (velocity, input, target yaw), movement (UE CalcVelocity:
//                         acceleration, braking, friction from the gait curves), the capsule (Fixed)
//
// The pose is made per frame by a controller of the animation module that reads LocomotionCharacter
// (locomotion:animation); the entity's Transform is interpolated between the last two ticks.
// Rotation reads two curves of the entity's anim::Animator (when it has one): RotationYawOffset (moving in
// view direction) and RotationYawSpeed (turning in place), like ALS reads its animation instance.
export namespace loco
{
    // system sets: input (FixedPre), simulation (Fixed), presentation (Update: transform, animation)
    struct LocomotionInputSet {};
    struct LocomotionSimulation {};
    struct LocomotionPresentation {};

    struct [[=scene::runtime_only]] LocomotionInput
    {
        glm::vec2 move_axis{0.0f};          // x: right, y: forward, relative to view_yaw (length <= 1)
        [[=rh::edit, =rh::read_only]] float view_yaw = 0.0f;      // degrees (ALS yaw)
        [[=rh::edit, =rh::read_only]] float view_pitch = 0.0f;    // degrees, positive looks up

        // held
        bool sprint = false;
        bool jump = false;
        bool aim = false;
        // pressed this tick
        bool toggle_walk = false;
        bool toggle_crouch = false;
        bool cycle_rotation_mode = false;
        bool roll = false;
        // the desired rotation mode, when the input sets it (the player: cvar loco.player.velocity_direction)
        bool set_rotation_mode = false;
        RotationMode rotation_mode = RotationMode::view_direction;
    };

    // Reads LocomotionInput from the keyboard (ALS layout: WASD, Shift hold sprint / tap roll, Alt walk,
    // Ctrl crouch, Space jump, right mouse aim, 1 rotation mode: toggles cvar loco.player.velocity_direction). The camera owner sets camera_yaw / pitch.
    struct LocomotionPlayer
    {
        [[=rh::edit]] bool enabled = true;
        [[=rh::edit, =rh::degrees]] float camera_yaw = 0.0f;     // radians, Rhea camera (looks -Z at 0)
        [[=rh::edit, =rh::degrees]] float camera_pitch = 0.0f;

        // key edges
        [[=rh::transient]] float shift_held = -1.0f;   // seconds Shift is down, < 0 up
        [[=rh::transient]] bool alt_was_down = false;
        [[=rh::transient]] bool ctrl_was_down = false;
        [[=rh::transient]] bool one_was_down = false;
    };

    struct ViewState
    {
        float yaw = 0.0f;
        float pitch = 0.0f;
        float yaw_speed = 0.0f;           // degrees / s
        float previous_yaw = 0.0f;
    };

    struct LocomotionState
    {
        bool has_input = false;
        float input_yaw = 0.0f;
        bool has_velocity = false;
        float speed = 0.0f;               // horizontal, m/s
        glm::vec3 velocity{0.0f};
        float velocity_yaw = 0.0f;
        bool moving = false;
        bool rotation_towards_last_input_direction_blocked = true;
        float target_yaw = 0.0f;
        float smooth_target_yaw = 0.0f;
        float target_yaw_view_space = 0.0f;
        float aiming_yaw_angle_limit = 180.0f;
        bool aiming_limit_applied_this_frame = false;
        bool reset_aiming_limit = true;
    };

    struct [[=scene::runtime_only]] LocomotionCharacter
    {
        std::shared_ptr<const LocomotionSettings> settings;
        [[=rh::edit, =rh::read_only]] float size_scale = 1.0f;     // character size / ALS mannequin size

        // ---- desired (from input)
        [[=rh::edit]] Gait desired_gait = Gait::running;
        [[=rh::edit]] Stance desired_stance = Stance::standing;
        [[=rh::edit]] RotationMode desired_rotation_mode = RotationMode::view_direction;
        [[=rh::edit]] bool desired_aiming = false;
        [[=rh::edit]] ViewMode view_mode = ViewMode::third_person;

        // ---- actual
        [[=rh::edit, =rh::read_only]] LocomotionMode locomotion_mode = LocomotionMode::grounded;
        [[=rh::edit, =rh::read_only]] RotationMode rotation_mode = RotationMode::view_direction;
        [[=rh::edit, =rh::read_only]] Stance stance = Stance::standing;
        [[=rh::edit, =rh::read_only]] Gait gait = Gait::walking;
        [[=rh::edit, =rh::read_only]] Gait max_allowed_gait = Gait::running;
        [[=rh::edit, =rh::read_only]] LocomotionAction action = LocomotionAction::none;

        ViewState view;
        LocomotionState locomotion;

        // ---- movement (UE character movement component)
        glm::vec3 position{0.0f};          // feet
        glm::vec3 previous_position{0.0f};
        [[=rh::edit, =rh::read_only]] float yaw = 0.0f;      // actor rotation, degrees (ALS yaw)
        float previous_yaw = 0.0f;
        glm::vec3 velocity{0.0f};          // m/s, after collisions
        glm::vec3 acceleration{0.0f};      // m/s^2, change of velocity over the last tick
        glm::vec3 input_direction{0.0f};   // world, length <= 1
        float desired_velocity_yaw = 0.0f;
        bool has_desired_velocity = false;
        [[=rh::edit, =rh::read_only]] float gait_amount = 0.0f;   // 0 still, 1 walk, 2 run, 3 sprint
        GaitSettings gait_settings;        // of the current rotation mode and stance, scaled
        float max_speed = 3.75f;
        float max_acceleration = 20.0f;
        float braking_deceleration = 15.0f;
        float ground_friction = 4.0f;
        float braking_friction_factor = 0.0f;
        float braking_friction_reset_timer = 0.0f;

        bool jump_was_down = false;
        float air_time = 0.0f;
        uint32_t jump_count = 0;           // events for the animation (per frame vs per tick): counters
        uint32_t land_count = 0;
        float land_vertical_speed = 0.0f;  // m/s at the last landing (negative)

        phys::CharacterId capsule;
        float capsule_height = 1.8f;

        // scaled settings
        float scaled(float distance) const { return distance * size_scale; }
    };

    // Makes the entity a locomotion character: capsule at its world transform, settings scaled by the size of
    // its skeleton (pelvis height) when it has a skinned mesh. Add LocomotionPlayer to drive it from the keyboard.
    bool init_locomotion_character(World& world, ecs::Entity e, const std::string& settings_path);

    // Over the shoulder camera of a locomotion character (cvars camera.*): the pivot above the character moves
    // to the right shoulder, the camera backs off from it; both pulled in front of walls. The middle of the
    // screen (where the weapons aim) is beside the character, not behind it. aim 0..1 blends to the aiming
    // camera: closer and tighter on the shoulder (the field of view: camera_fov_scale).
    Transform make_locomotion_camera(World& world, ecs::Entity character, float camera_yaw, float camera_pitch,
        float aim = 0.0f);
    // field of view multiplier of the camera at aim 0..1
    float camera_fov_scale(float aim);

    // Automation: while set, LocomotionPlayer characters take this input instead of the keyboard
    // (view_yaw / pitch still come from the player's camera)
    void set_scripted_locomotion_input(const std::optional<LocomotionInput>& input);

    // cvar loco.debug.log: state changes of characters and their animation go to the log
    bool debug_log_enabled();
}
