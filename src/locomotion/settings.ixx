export module locomotion:settings;

import std.compat;
import glm;
import :types;

#include "common/type_macros.h"

// Tuning of a locomotion character (ALS UAlsCharacterSettings + UAlsMovementSettings + the character movement
// component defaults), from assets/locomotion/*.json written by tools/ue_import/import_als_settings.py.
// Distances and speeds are for a character whose pelvis is at reference_pelvis_height: the character scales
// them by its own size (LocomotionCharacter::size_scale).
export namespace loco
{
    // samples of a curve, linear between them
    struct SampledCurve
    {
        std::vector<float> times;
        std::vector<glm::vec3> values;   // x only for float curves

        glm::vec3 evaluate(float t) const;
        float evaluate_x(float t) const { return evaluate(t).x; }
    };

    struct GaitSettings
    {
        float walk_forward_speed = 1.75f;
        float walk_backward_speed = 1.75f;
        float run_forward_speed = 3.75f;
        float run_backward_speed = 3.75f;
        float sprint_speed = 6.5f;
        bool direction_dependent_speed = false;
        const SampledCurve* acceleration_deceleration_friction = nullptr;   // by gait amount 0..3
        const SampledCurve* rotation_half_life = nullptr;                   // by gait amount 0..3

        float max_walk_speed() const { return direction_dependent_speed ? std::max(walk_forward_speed, walk_backward_speed) : walk_forward_speed; }
        float max_run_speed() const { return direction_dependent_speed ? std::max(run_forward_speed, run_backward_speed) : run_forward_speed; }
    };

    // UAlsAnimationInstanceSettings, in ALS units (cm, cm/s, degrees): the animation converts speeds with the
    // character size (speed_cm = speed_m * 100 / size_scale)
    struct AnimationSettings
    {
        float moving_smooth_speed_threshold = 150.0f;
        float lean_interpolation_half_life = 0.2f;
        float velocity_blend_interpolation_half_life = 0.1f;
        const SampledCurve* rotation_yaw_offset_forward = nullptr;    // by velocity yaw in view space
        const SampledCurve* rotation_yaw_offset_backward = nullptr;
        const SampledCurve* rotation_yaw_offset_left = nullptr;
        const SampledCurve* rotation_yaw_offset_right = nullptr;
        float animated_walk_speed = 150.0f;
        float animated_run_speed = 350.0f;
        float animated_sprint_speed = 600.0f;
        const SampledCurve* stride_blend_walk = nullptr;              // by speed
        const SampledCurve* stride_blend_run = nullptr;
        float pivot_activation_speed_threshold = 200.0f;
        float animated_crouch_speed = 150.0f;
        const SampledCurve* crouching_stride_blend = nullptr;
        const SampledCurve* in_air_lean_amount = nullptr;             // by vertical speed
        const SampledCurve* ground_prediction_amount = nullptr;       // by sweep time
        float quick_stop_blend_in = 0.1f, quick_stop_blend_out = 0.2f;
        glm::vec2 quick_stop_play_rate{1.75f, 3.0f};
        float quick_stop_start_time = 0.3f;
        float dynamic_transition_foot_lock_distance_threshold = 8.0f;
        float dynamic_transition_blend_duration = 0.2f;
        float dynamic_transition_play_rate = 1.5f;
        float rotate_in_place_view_yaw_angle_threshold = 50.0f;
        float rotate_in_place_first_person_view_yaw_angle_threshold = 65.0f;
        glm::vec2 rotate_in_place_reference_view_yaw_speed{180.0f, 460.0f};
        glm::vec2 rotate_in_place_play_rate{1.15f, 3.0f};
        float turn_in_place_view_yaw_angle_threshold = 45.0f;
        float turn_in_place_view_yaw_speed_threshold = 50.0f;
        glm::vec2 turn_in_place_view_yaw_angle_to_activation_delay{0.0f, 0.75f};
        float turn_in_place_turn_180_angle_threshold = 130.0f;
        float turn_in_place_blend_duration = 0.2f;
        float turn_in_place_play_rate = 1.2f;
        const SampledCurve* stance_change_curve = nullptr;            // blend curve of stance changes, 0..1
        bool allow_foot_lock = true;
        float foot_lock_thigh_angle_limit = 90.0f;
        float foot_lock_foot_angle_limit = 40.0f;
        float head_pitch_angle_half_life = 0.1f;
        float head_yaw_angle_half_life = 0.1f;
        float head_switch_look_sides_yaw_angle_half_life = 0.2f;
        float head_first_person_pitch_angle_half_life = 0.01f;
        float head_first_person_yaw_angle_half_life = 0.01f;
    };

    // FAlsRollingSettings (meters)
    struct RollingSettings
    {
        std::string montage;
        float play_rate = 1.3f;
        bool crouch_on_start = true;
        bool rotate_to_input_on_start = true;
        float rotation_half_life = 0.1f;
        bool start_on_land = true;
        float on_land_speed_threshold = 7.0f;
        bool interrupt_when_in_air = true;
    };

    // FAlsMantlingTraceSettings (meters)
    struct MantlingTraceSettings
    {
        glm::vec2 ledge_height{0.5f, 2.25f};
        float reach_distance = 0.75f;
        float target_location_offset = 0.15f;
        float start_location_offset = 0.55f;
    };

    // UAlsMantlingSettings: the montage and where it starts by the ledge height (meters)
    struct MantlingTypeSettings
    {
        std::string montage;
        glm::vec2 start_time_reference_height{0.5f, 1.0f};
        glm::vec2 start_time{0.5f, 0.0f};
        glm::vec2 warp_time_range{0.0f, 0.3f};     // montage time over which the root is warped onto the ledge
    };

    // FAlsGeneralMantlingSettings (meters, degrees)
    struct MantlingSettings
    {
        bool allow = true;
        bool auto_start_in_air = true;
        float trace_angle_threshold = 110.0f;
        float max_reach_angle = 50.0f;
        float slope_angle_threshold = 35.0f;
        float high_height_threshold = 1.25f;
        float blend_out_duration = 0.3f;
        MantlingTraceSettings grounded_trace;
        MantlingTraceSettings in_air_trace{ .ledge_height = {0.5f, 1.5f}, .reach_distance = 0.7f };
        MantlingTypeSettings types[3];             // [MantlingType]
    };

    struct LocomotionSettings
    {
        DEFAULT_NON_COPYABLE(LocomotionSettings)

        std::string path;
        float reference_pelvis_height = 0.9675f;

        // character
        float moving_speed_threshold = 0.5f;
        InAirRotationMode in_air_rotation_mode = InAirRotationMode::rotate_to_velocity;
        bool allow_aiming_when_in_air = true;
        bool sprint_has_priority_over_aiming = false;
        bool rotate_to_velocity_when_sprinting = false;
        bool rotate_towards_desired_velocity = true;
        bool auto_rotate_on_any_input_while_not_moving = true;
        float aiming_yaw_angle_limit = 70.0f;

        // movement
        glm::vec2 velocity_angle_to_speed_range{100.0f, 125.0f};
        GaitSettings gaits[3][2];      // [RotationMode][Stance]

        // physics
        float capsule_radius = 0.3f;
        float standing_height = 1.8f;
        float crouched_height = 1.12f;
        float jump_velocity = 4.2f;
        float gravity = 9.8f;
        float air_control = 0.15f;
        float air_acceleration = 20.0f;

        AnimationSettings animation;
        RollingSettings rolling;
        MantlingSettings mantling;

        std::unordered_map<std::string, SampledCurve> curves;

        const GaitSettings& gait(RotationMode mode, Stance stance) const { return gaits[(int)mode][(int)stance]; }
    };

    // cached by path; null when the file is missing or broken
    std::shared_ptr<const LocomotionSettings> load_locomotion_settings(const std::string& rel_path);
}
