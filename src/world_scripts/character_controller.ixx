module;

#include <json/value.h>

export module character_controller;

import std.compat;

import glm;
import rhmath;
import ecs;
import framework;
import assets;
import input;
import rhcomponents;
import physics;
import reflect;

// Third person character of a skeletal mesh entity, moved as a physics character capsule
// (collides with the level, climbs steps, falls from ledges):
//  * WASD relative to the camera, LeftShift walks (run by default), Space jumps,
//  * idle / walk / run blended by speed (walk and run are phase synchronized),
//  * jump start / loop / end with simple ballistics,
//  * movement speed matches the ground speed of the clips (no foot sliding).
// Clips and their measured ground speeds come from a locomotion json (tools/ue_import).
//
// Split for the fixed tick (and later the network):
//   CharacterInput     - the command of one tick: filled from the keyboard for PlayerControlled (FixedPre)
//   CharacterMovement  - simulation state, advanced by move_characters every fixed tick (Fixed)
//   CharacterAnimator  - pose from the movement state, every frame (Update); the entity's Transform is
//                        interpolated between the last two ticks at the same time
export
{
    struct CharacterInput
    {
        glm::vec2 move_axis{0.0f};   // x: right, y: forward, relative to camera_yaw
        [[=rh::edit, =rh::read_only, =rh::degrees]] float camera_yaw = 0.0f;
        [[=rh::edit, =rh::read_only]] bool walk = false;
        [[=rh::edit, =rh::read_only]] bool jump = false;           // held
    };

    // Reads CharacterInput from the keyboard. camera_yaw is set by whoever drives the camera.
    struct PlayerControlled
    {
        [[=rh::edit]] bool enabled = true;   // false: no input (free camera)
        [[=rh::edit, =rh::degrees]] float camera_yaw = 0.0f;
    };

    struct CharacterMovement
    {
        glm::vec3 position{0.0f};   // feet
        glm::vec3 move_direction{0, 0, 1};
        [[=rh::edit, =rh::read_only, =rh::degrees]] float heading = 0.0f;   // yaw, 0 faces +Z
        [[=rh::edit, =rh::read_only]] float speed = 0.0f;                   // horizontal, m/s
        float vertical_velocity = 0.0f;
        [[=rh::edit, =rh::read_only]] bool grounded = true;
        bool jump_was_down = false;
        float air_time = 0.0f;        // since the capsule left the ground

        // events for the animation (it runs per frame, the movement per tick): counters, compared with the last seen
        uint32_t jump_count = 0;
        uint32_t land_count = 0;

        // state of the previous tick, for interpolation
        glm::vec3 previous_position{0.0f};
        float previous_heading = 0.0f;

        float walk_speed = 1.0f;      // ground speeds of the walk / run clips
        float run_speed = 2.0f;

        phys::CharacterId capsule;

        static constexpr float acceleration = 9.0f;
        static constexpr float deceleration = 12.0f;
        static constexpr float turn_rate = 12.0f;      // rad/s
        static constexpr float jump_velocity = 4.2f;   // m/s
        static constexpr float gravity = 12.0f;        // m/s^2
        static constexpr float capsule_radius = 0.3f;
    };

    struct CharacterAnimator
    {
        struct Clip
        {
            AnimationClipHandle handle;
            AnimationBinding binding;
            float duration = 0.0f;
            float ground_speed = 0.0f;
            std::string name;

            bool valid() const { return handle.is_valid(); }
        };

        struct Expression
        {
            std::string name;
            std::vector<float> weights;   // one per morph target of the mesh
        };

        enum class JumpState { none, start, loop, land };

        Clip idle, walk, run, jump_start, jump_loop, jump_end;

        std::vector<Clip> poses;
        [[=rh::edit, =rh::read_only]] int32_t active_pose = -1;      // pose being blended in (or -1)
        int32_t fading_pose = -1;      // previous pose, blended out
        float pose_time = 0.0f;
        float fading_pose_time = 0.0f;
        float pose_weight = 0.0f;
        float fading_pose_weight = 0.0f;

        std::vector<Expression> expressions;
        [[=rh::edit, =rh::read_only]] int32_t active_expression = -1;
        std::vector<float> expression_from;     // weights when the blend started
        std::vector<float> expression_weights;  // current weights
        float expression_blend = 1.0f;          // 0..1 from -> target

        BonePose bind_pose;
        BonePose pose_a, pose_b, pose_locomotion, pose_final;

        float idle_time = 0.0f;
        [[=rh::edit, =rh::read_only]] float locomotion_phase = 0.0f;  // normalized walk/run cycle
        JumpState jump_state = JumpState::none;
        float jump_time = 0.0f;
        [[=rh::edit, =rh::read_only]] float jump_weight = 0.0f;
        uint32_t seen_jump_count = 0;
        uint32_t seen_land_count = 0;

        static constexpr float jump_blend_time = 0.12f;
        static constexpr float pose_blend_time = 0.25f;
        static constexpr float expression_blend_time = 0.2f;
        static constexpr float fall_animation_delay = 0.15f;   // walking off small ledges stays in locomotion

        // Optional set of looping poses (poses.json from tools/ue_import), selected by index
        void load_poses(const std::string& poses_json_path, const Skeleton& skeleton);
        // -1 returns to locomotion. Moving also leaves the pose.
        void select_pose(int32_t pose_index);
        int32_t get_pose_count() const { return (int32_t)poses.size(); }
        const std::string& get_pose_name(int32_t pose_index) const { return poses[pose_index].name; }

        // Facial expressions (morph target weights by ARKit-style name), selected by index.
        // Format: {"expressions": [{"name": "...", "weights": {"eyeBlinkLeft": 1.0, ...}}]}
        void load_expressions(const std::string& expressions_json_path, const SkinnedMesh& mesh);
        // -1 returns to the neutral face. Expressions are independent of poses.
        void select_expression(int32_t expression_index);
        int32_t get_expression_count() const { return (int32_t)expressions.size(); }
        const std::string& get_expression_name(int32_t expression_index) const { return expressions[expression_index].name; }
    };

    // Makes the entity (SkinnedMesh, Transform) a character: loads the locomotion clips, creates the capsule.
    // Add PlayerControlled to drive it from the keyboard.
    bool init_character(World& world, ecs::Entity e, const std::string& locomotion_json_path);

    // Orbit camera around the character, pulled in front of walls
    Transform make_character_camera(World& world, ecs::Entity character, float camera_yaw, float camera_pitch);

    // Component types, systems; `input` (the keyboard of PlayerControlled characters) becomes a resource
    void install_character_controller(World& world, const Input& input);
}
