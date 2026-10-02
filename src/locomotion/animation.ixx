export module locomotion:animation;

import std.compat;
import glm;
import assets;
import ecs;
import framework;
import reflect;
import animation;
import physics;
import :types;
import :character;

// The pose of a locomotion character: the ALS animation graph (AB_Als and its linked graphs) written with the
// animation module DSL, fed by the animation state of UAlsAnimationInstance (ported in locomotion_animation.cpp
// and animation_*.cpp). Runs per frame on the entity's anim::Animator (set anim::Evaluate), reads LocomotionCharacter.
//
//   grounded (AB_Als_Grounded / Standing / Crouching)      locomotion_animation.cpp
//   -> Transition slot -> in air (AB_Als_Locomotion)        animation_in_air.cpp
//   -> PostLocomotion slot (rolls, mantles)
//   -> layering with the overlay (AB_Als_Layering, overlays) -> head (AB_Als_Head)   animation_layering.cpp
//   -> control rig: spine rotation, foot lock, foot IK, pelvis offset (CR_Als)       animation_feet.cpp
export namespace loco
{
    // ALS animation curves (UAlsConstants): clips write them, the graph and the character read them
    inline constexpr std::array<std::string_view, 43> curve_names = {
        "AllowTransitions", "FeetCrossing", "FootLeftIk", "FootLeftLock", "FootPlanted", "FootRightIk", "FootRightLock",
        "FootstepSoundBlock", "GroundPredictionBlock", "HandLeftIk", "HandRightIk", "HipsDirectionLock",
        "LayerArmLeft", "LayerArmLeftAdditive", "LayerArmLeftLocalSpace", "LayerArmLeftSlot",
        "LayerArmRight", "LayerArmRightAdditive", "LayerArmRightLocalSpace", "LayerArmRightSlot",
        "LayerHandLeft", "LayerHandRight", "LayerHead", "LayerHeadAdditive", "LayerHeadSlot",
        "LayerLegs", "LayerLegsSlot", "LayerPelvis", "LayerPelvisSlot", "LayerSpine", "LayerSpineAdditive", "LayerSpineSlot",
        "PoseAiming", "PoseCrouching", "PoseGait", "PoseGrounded", "PoseInAir", "PoseMoving", "PoseStanding",
        "RotationYawOffset", "RotationYawSpeed", "SprintBlock", "ViewBlock",
    };

    // indices of curve_names in the rig's curve set
    namespace curve
    {
        enum : uint32_t
        {
            AllowTransitions, FeetCrossing, FootLeftIk, FootLeftLock, FootPlanted, FootRightIk, FootRightLock,
            FootstepSoundBlock, GroundPredictionBlock, HandLeftIk, HandRightIk, HipsDirectionLock,
            LayerArmLeft, LayerArmLeftAdditive, LayerArmLeftLocalSpace, LayerArmLeftSlot,
            LayerArmRight, LayerArmRightAdditive, LayerArmRightLocalSpace, LayerArmRightSlot,
            LayerHandLeft, LayerHandRight, LayerHead, LayerHeadAdditive, LayerHeadSlot,
            LayerLegs, LayerLegsSlot, LayerPelvis, LayerPelvisSlot, LayerSpine, LayerSpineAdditive, LayerSpineSlot,
            PoseAiming, PoseCrouching, PoseGait, PoseGrounded, PoseInAir, PoseMoving, PoseStanding,
            RotationYawOffset, RotationYawSpeed, SprintBlock, ViewBlock,
        };
    }

    enum class HipsDirection : uint8_t { forward, backward, right_forward, right_backward, left_forward, left_backward };
    enum class MovementDirection : uint8_t { forward, backward, left, right };
    enum class GroundedEntryMode : uint8_t { none, from_roll };

    struct LocomotionClips;

    // ---- animation state (UAlsAnimationInstance members)
    struct AnimPoseState            // from the curves of the last pose
    {
        float grounded = 0.0f, in_air = 0.0f, standing = 0.0f, crouching = 0.0f, moving = 0.0f;
        float gait = 0.0f, gait_walking = 0.0f, gait_running = 0.0f, gait_sprinting = 0.0f;
        float unweighted_gait = 0.0f, unweighted_walking = 0.0f, unweighted_running = 0.0f, unweighted_sprinting = 0.0f;
    };

    struct AnimViewState
    {
        float yaw_world = 0.0f, pitch_world = 0.0f, yaw_speed = 0.0f;
        float yaw_angle = 0.0f;         // relative to the actor
        float pitch_angle = 0.0f;
        float pitch_amount = 0.5f;
        float head_blend = 0.0f;        // look at the view with the head (not while aiming)
    };

    struct AnimLocomotionState
    {
        bool has_input = false;
        float input_yaw = 0.0f;
        float speed = 0.0f;             // m/s
        float speed_cm = 0.0f;          // ALS units: cm/s of the mannequin (speed / size)
        glm::vec3 velocity{0.0f};
        float velocity_yaw = 0.0f;
        glm::vec3 acceleration{0.0f};
        float max_acceleration = 0.0f;
        float max_braking_deceleration = 0.0f;
        bool moving = false;
        bool moving_smooth = false;
        float target_yaw = 0.0f;
        glm::vec3 position{0.0f};       // feet, interpolated like the transform
        float yaw = 0.0f;               // actor, interpolated like the transform
        float yaw_velocity = 0.0f;      // degrees / s
        float scale = 1.0f;
    };

    struct AnimGroundedState
    {
        HipsDirection hips_direction = HipsDirection::forward;
        float hips_direction_lock = 0.0f;
        bool velocity_blend_initialization_required = true;
        float forward = 0.0f, backward = 0.0f, left = 0.0f, right = 0.0f;     // velocity blend
        MovementDirection movement_direction = MovementDirection::forward;
        float yaw_offset_forward = 0.0f, yaw_offset_backward = 0.0f, yaw_offset_left = 0.0f, yaw_offset_right = 0.0f;
    };

    struct AnimStandingState
    {
        float stride_blend = 0.0f;
        float walk_run_blend = 0.0f;
        float play_rate = 1.0f;
        float sprint_block = 0.0f;
        float sprint_time = 0.0f;
        float sprint_acceleration = 0.0f;
        bool pivot_active = false;
    };

    struct AnimCrouchingState
    {
        float stride_blend = 0.0f;
        float play_rate = 1.0f;
    };

    struct AnimInAirState
    {
        uint32_t jump_count = 0;        // LocomotionCharacter::jump_count seen last
        bool jumped = false;            // a jump started since the last in air update
        float jump_play_rate = 1.2f;
        float vertical_velocity = 0.0f; // ALS cm/s, of the last in air update (the landing speed after landing)
        float ground_prediction = 0.0f; // 0..1, ground below about to be hit
    };

    // blend amounts of the body parts from the overlay over locomotion (Layer* curves of the last pose)
    struct AnimLayeringState
    {
        float head = 0.0f, head_additive = 0.0f, head_slot = 1.0f;
        float arm_left = 0.0f, arm_left_additive = 0.0f, arm_left_slot = 1.0f, arm_left_local_space = 0.0f, arm_left_mesh_space = 0.0f;
        float arm_right = 0.0f, arm_right_additive = 0.0f, arm_right_slot = 1.0f, arm_right_local_space = 0.0f, arm_right_mesh_space = 0.0f;
        float hand_left = 0.0f, hand_right = 0.0f;
        float spine = 0.0f, spine_additive = 0.0f, spine_slot = 1.0f;
        float pelvis = 0.0f, pelvis_slot = 1.0f;
        float legs = 0.0f, legs_slot = 1.0f;
    };

    // spine yaw towards the view while aiming (applied by the control rig)
    struct AnimSpineState
    {
        bool rotation_allowed = false;
        float amount = 0.0f, amount_scale = 1.0f, amount_bias = 0.0f;
        float last_yaw_angle = 0.0f, last_yaw_angle_world = 0.0f;
        float yaw_angle = 0.0f;
        float final_yaw_angle = 0.0f;
    };

    struct AnimHeadState
    {
        bool initialization_required = true;
        bool switching_look_sides = false;
        float pitch_angle = 0.0f;
        float yaw_angle = 0.0f, yaw_velocity = 0.0f;
        float yaw_amount = 0.5f;        // normalized time of the look blend space: 0 = -180, 1 = 180 degrees
    };

    // One foot: where the animation puts it, where it is locked, the IK target, the ground under it.
    // Positions: world or model (skeleton root) space as named.
    struct FootState
    {
        float lock_amount = 0.0f;
        glm::vec3 thigh_axis_pelvis{0.0f};                  // thigh direction in pelvis space (reference pose)
        glm::vec3 target_location{0.0f};                    // world, animated foot
        glm::quat target_rotation{1, 0, 0, 0};
        glm::vec3 lock_location_world{0.0f};
        glm::quat lock_rotation_world{1, 0, 0, 0};
        glm::vec3 lock_location{0.0f};                      // model
        glm::quat lock_rotation{1, 0, 0, 0};
        glm::vec3 final_location{0.0f};                     // model, IK target
        glm::quat final_rotation{1, 0, 0, 0};

        // control rig
        float offset_z = 0.0f;                              // ground height under the foot, model
        glm::vec3 offset_normal{0, 1, 0};                   // ground normal, model
        bool ik_initialized = false;
        float ik_offset_z = 0.0f, ik_offset_velocity = 0.0f, ik_previous_target = 0.0f;
        bool rotation_initialized = false;
        glm::vec3 damped_normal{0, 1, 0};
        glm::vec3 pole{0.0f};                               // knee target, model
        bool pole_initialized = false;
    };

    struct AnimFeetState
    {
        // bones of the control rig (-1: missing, no control rig)
        struct Bones
        {
            int32_t pelvis = -1;
            int32_t thigh[2] = { -1, -1 }, calf[2] = { -1, -1 }, foot[2] = { -1, -1 };   // left, right
            std::vector<uint32_t> spine;                    // spine_01.. (the view rotation is spread over them)
        };
        Bones bones;
        bool valid = false;
        bool became_valid = false;
        glm::quat pelvis_rotation{1, 0, 0, 0};              // model
        FootState left, right;
        float pelvis_offset = 0.0f;                         // applied, model up
        float pelvis_spring = 0.0f, pelvis_spring_velocity = 0.0f;
        bool pelvis_initialized = false;
        float leg_length = 0.0f;                            // thigh -> foot, bind pose
        float foot_height = 0.0f;                           // ankle above the ground, bind pose
        glm::vec3 previous_position{0.0f};
    };

    // States of the weapon overlays (AB_Als_Rifle, AB_Als_PistolTwoHanded): relaxed (carried), ready (raised, for
    // 3 s after aiming), aiming (right mouse: the weapon follows the view pitch)
    enum class WeaponOverlayState : uint8_t { relaxed, ready, aiming };

    // A weapon overlay: frames of its poses clip per state, the aim additives, the arms of running / sprinting.
    // -1: the frame is not used (a fallback is said per field).
    struct WeaponOverlay
    {
        int32_t relaxed_stand = 0;
        int32_t relaxed_walk = 1;          // walking (and running without run_arms)
        int32_t relaxed_sprint = -1;       // without sprint_arms; -1: relaxed_walk
        int32_t relaxed_crouch = 2;
        int32_t air = -1;                  // in air, far from the ground; -1: the grounded poses
        int32_t air_landing = -1;          // in air, the ground predicted close
        int32_t ready_stand = 0, ready_walk = 1, ready_crouch = 2;
        int32_t aim_stand = 0, aim_walk = 1, aim_crouch = 2;
        AnimationClipHandle aim_clip, aim_crouch_clip;  // mesh space additives, time = the view's pitch amount
        AnimationClipHandle run_arms, sprint_arms;      // optional, synced with the "Movement" group
    };

    // Data driven overlays: the ALS overlay graphs that are pose clips + idle secondary motion (Default, Feminine,
    // Masculine; poses clip frames: 0 standing, 1 standing walking (and in air), 2 crouching), and the weapon
    // overlays (weapon set: Rifle, PistolTwoHanded).
    struct OverlayDefinition
    {
        std::string name;
        AnimationClipHandle poses;
        AnimationClipHandle idle;       // local additive secondary motion
        float idle_alpha = 0.75f;
        std::shared_ptr<const WeaponOverlay> weapon;
    };

    struct [[=scene::runtime_only]] LocomotionAnimation
    {
        std::shared_ptr<const LocomotionClips> clips;
        bool pending_update = true;     // first update: no rates of change, no interpolation

        // ---- copies of the character state
        LocomotionMode locomotion_mode = LocomotionMode::grounded;
        RotationMode rotation_mode = RotationMode::view_direction;
        ViewMode view_mode = ViewMode::third_person;
        Stance stance = Stance::standing;
        Gait gait = Gait::walking;
        LocomotionAction action = LocomotionAction::none;
        GroundedEntryMode grounded_entry = GroundedEntryMode::none;

        [[=rh::edit]] int32_t overlay = 0;   // index of LocomotionClips::overlays
        [[=rh::edit]] bool control_rig = true;   // spine rotation, foot lock / IK, pelvis offset

        // ---- animation state
        AnimPoseState pose;
        AnimViewState view;
        AnimLocomotionState locomotion;
        AnimGroundedState grounded;
        AnimStandingState standing;
        AnimCrouchingState crouching;
        AnimInAirState in_air;
        AnimLayeringState layering;
        AnimSpineState spine;
        AnimHeadState head;
        AnimFeetState feet;
        float lean_right = 0.0f, lean_forward = 0.0f;
        float foot_planted = 0.0f, feet_crossing = 0.0f;
        bool transitions_allowed = false;
        int32_t dynamic_transition_frame_delay = 0;
        bool rotating_left = false, rotating_right = false;
        float rotate_in_place_play_rate = 1.15f;
        float turn_in_place_activation_delay = 0.0f;
        float turn_in_place_play_rate = 1.0f;

        // ---- rig data
        anim::BoneMask leg_left_mask, leg_right_mask;
        anim::BlendProfile move_direction_change_profile, quick_feet_profile;
        anim::CustomCurve stance_change_curve;
        phys::Shape ground_prediction_shape;             // standing capsule

        // ---- graph nodes
        struct Standing
        {
            anim::BlendSpacePlayer forward, backward, left_forward, left_backward, right_forward, right_backward;
            anim::SequencePlayer sprint, sprint_acceleration;
            anim::BlendList sprint_list;
            anim::TwoWayBlend sprint_acceleration_blend;
            anim::ScaleBiasClamp sprint_acceleration_alpha;
            anim::BlendSpacePlayer lean;
            anim::ScaleBiasClamp lean_alpha;

            enum class Move { forward, backward, right_forward, right_backward, left_forward, left_backward };
            anim::StateMachine<Move> movement{Move::forward};
            anim::NodeState movement_relevance;

            enum class Detail { walk, run, run_start, run_start_from_walk, first_pivot, second_pivot };
            anim::StateMachine<Detail> details{Detail::walk};
            anim::NodeState details_relevance;
            float details_weight = 0.0f;            // of the machine in the whole graph, last frame
            std::array<anim::SequencePlayer, 4> run_start, run_start_from_walk, first_pivot, second_pivot;
            anim::Inertialization details_inertialization;
            anim::NodeState standing_movement_relevance;

            enum class State { idle, move, stop, rotate_left, rotate_right };
            anim::StateMachine<State> states{State::idle};
            anim::NodeState states_relevance;
            anim::Inertialization states_inertialization;
            anim::NodeState idle_relevance;
            anim::SequencePlayer rotate_left, rotate_right;

            enum class Stop { entry, lock_left, lock_right, plant_left, plant_right };
            anim::StateMachine<Stop> stop{Stop::entry};
            anim::NodeState stop_relevance;
            std::array<anim::BlendList, 4> plant_hips;   // left foot: left / right side poses, right foot: same
        };
        Standing standing_nodes;

        struct Crouching
        {
            std::array<anim::SequencePlayer, 6> walk;    // F, B, LF, LB, RF, RB
            anim::StateMachine<Standing::Move> movement{Standing::Move::forward};
            anim::NodeState movement_relevance;
            anim::ScaleBiasClamp stride_alpha;
            anim::BlendSpacePlayer lean;

            enum class State { idle, move, stop, rotate_left, rotate_right };
            anim::StateMachine<State> states{State::idle};
            anim::NodeState states_relevance;
            anim::Inertialization states_inertialization;
            anim::NodeState idle_relevance;
            anim::SequencePlayer rotate_left, rotate_right;
        };
        Crouching crouching_nodes;

        struct Grounded
        {
            enum class State { standing, crouching, standing_to_crouching, crouching_to_standing, roll };
            anim::StateMachine<State> states{State::standing};
            anim::NodeState relevance;
            anim::Inertialization inertialization;
            anim::SequencePlayer stand_to_crouch, crouch_to_stand;
        };
        Grounded grounded_nodes;

        // AB_Als_Locomotion
        struct InAir
        {
            enum class State { grounded, fall, jump, land, land_movement };
            anim::StateMachine<State> states{State::grounded};
            anim::NodeState relevance;
            anim::Inertialization inertialization;

            // fall
            anim::SequencePlayer fall, fall_fast, flail;
            anim::ScaleBiasClamp fall_fast_alpha, flail_alpha, fall_ground_prediction_alpha;
            anim::BlendSpacePlayer fall_lean;

            // jump
            enum class Jump { left_foot, right_foot, loop, flail };
            anim::StateMachine<Jump> jump{Jump::left_foot};
            anim::NodeState jump_relevance;
            anim::SequencePlayer jump_walk_left, jump_run_left, jump_walk_right, jump_run_right, jump_loop, jump_flail;
            anim::ScaleBiasClamp jump_left_alpha, jump_right_alpha, jump_ground_prediction_alpha;
            anim::Inertialization jump_inertialization;
            anim::BlendSpacePlayer jump_lean;

            // land
            anim::SequencePlayer land_light, land_heavy;
            anim::SequencePlayer land_light_additive, land_heavy_additive;
            anim::ScaleBiasClamp land_aiming_alpha;
        };
        InAir in_air_nodes;

        // AB_Als_Layering, overlays, AB_Als_Head
        struct Layering
        {
            anim::BoneMask pelvis, spine, neck, arm_left, arm_right, hand_left, hand_right;
            anim::Inertialization overlay_inertialization;
            anim::BlendList overlays;
            std::vector<anim::SequencePlayer> overlay_idle;           // per overlay
            std::vector<anim::ScaleBiasClamp> overlay_ground_prediction;
            struct WeaponNodes
            {
                anim::NodeState relevance;
                anim::StateMachine<WeaponOverlayState> states{ WeaponOverlayState::relaxed };
                anim::SequencePlayer run_arms, sprint_arms;
            };
            std::vector<WeaponNodes> overlay_weapon;                 // per overlay (weapon overlays only)
            anim::NodeState head_relevance;
            anim::BlendSpacePlayer look;
        };
        Layering layering_nodes;
    };

    // Adds anim::Animator (rig of the entity's skinned mesh with the ALS curves) and LocomotionAnimation
    bool init_locomotion_animation(World& world, ecs::Entity e);

    // overlay by name (case insensitive), -1 when unknown
    int32_t find_overlay(const LocomotionAnimation& a, std::string_view name);
}
