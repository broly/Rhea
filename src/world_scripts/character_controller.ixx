module;

#include <json/value.h>

export module character_controller;

import std.compat;

import glm;
import rhmath;
import framework;
import assets;
import input;
import rhcomponents;

// Third person locomotion for a skeletal mesh actor, on a flat ground plane (y = 0):
//  * WASD relative to the camera, LeftShift walks (run by default), Space jumps,
//  * idle / walk / run blended by speed (walk and run are phase synchronized),
//  * jump start / loop / end with simple ballistics,
//  * movement speed matches the ground speed of the clips (no foot sliding).
// Clips and their measured ground speeds come from a locomotion json (tools/ue_import).
export class CharacterController
{
public:
    bool init(std::shared_ptr<RhActor> in_actor, const std::string& locomotion_json_path);
    
    // Optional set of looping poses (poses.json from tools/ue_import), selected by index.
    void load_poses(const std::string& poses_json_path);
    
    // -1 returns to locomotion. Moving also leaves the pose.
    void select_pose(int32_t pose_index);
    int32_t get_pose_count() const { return (int32_t)poses.size(); }
    int32_t get_active_pose() const { return active_pose; }
    const std::string& get_pose_name(int32_t pose_index) const { return poses[pose_index].name; }

    // Facial expressions (morph target weights by ARKit-style name), selected by index.
    // Format: {"expressions": [{"name": "...", "weights": {"eyeBlinkLeft": 1.0, ...}}]}
    void load_expressions(const std::string& expressions_json_path);

    // -1 returns to the neutral face. Expressions are independent of poses.
    void select_expression(int32_t expression_index);
    int32_t get_expression_count() const { return (int32_t)expressions.size(); }
    int32_t get_active_expression() const { return active_expression; }
    const std::string& get_expression_name(int32_t expression_index) const { return expressions[expression_index].name; }

    // input may be null: the character only plays idle / decelerates
    void tick(float dt, const Input* input, float camera_yaw);

    // orbit camera around the character
    Transform make_camera_transform(float camera_yaw, float camera_pitch) const;

    glm::vec3 get_position() const { return position; }

private:
    struct Clip
    {
        AnimationClipHandle handle;
        AnimationBinding binding;
        float duration = 0.0f;
        float ground_speed = 0.0f;

        std::string name;
        
        bool valid() const { return handle.is_valid(); }
    };

    enum class JumpState { none, start, loop, land };

    bool load_clip(const Json::Value& locomotion, const char* role, Clip& clip);
    void update_movement(float dt, const Input* input, float camera_yaw);
    void update_pose(float dt);
    void update_expression(float dt);
    void sample(const Clip& clip, float time, bool loop, BonePose& pose) const;

    std::shared_ptr<RhActor> actor;
    std::shared_ptr<RhComp_SkeletalMesh> mesh_component;

    Clip idle, walk, run, jump_start, jump_loop, jump_end;
    
    std::vector<Clip> poses;
    int32_t active_pose = -1;      // pose being blended in (or -1)
    int32_t fading_pose = -1;      // previous pose, blended out
    float pose_time = 0.0f;
    float fading_pose_time = 0.0f;
    float pose_weight = 0.0f;
    float fading_pose_weight = 0.0f;

    struct Expression
    {
        std::string name;
        std::vector<float> weights;   // one per morph target of the mesh
    };

    std::vector<Expression> expressions;
    int32_t active_expression = -1;
    std::vector<float> expression_from;     // weights when the blend started
    std::vector<float> expression_weights;  // current weights
    float expression_blend = 1.0f;          // 0..1 from -> target

    BonePose bind_pose;
    BonePose pose_a, pose_b, pose_locomotion, pose_final;

    // movement
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 move_direction = glm::vec3(0, 0, 1);
    float heading = 0.0f;         // yaw of the character, 0 faces +Z
    float speed = 0.0f;           // horizontal, m/s
    float vertical_velocity = 0.0f;
    bool grounded = true;
    bool jump_was_down = false;

    // animation state
    float idle_time = 0.0f;
    float locomotion_phase = 0.0f; // normalized walk/run cycle
    JumpState jump_state = JumpState::none;
    float jump_time = 0.0f;
    float jump_weight = 0.0f;

    // tuning
    static constexpr float acceleration = 9.0f;
    static constexpr float deceleration = 12.0f;
    static constexpr float turn_rate = 12.0f;      // rad/s
    static constexpr float jump_velocity = 4.2f;   // m/s
    static constexpr float gravity = 12.0f;        // m/s^2
    static constexpr float jump_blend_time = 0.12f;
    static constexpr float pose_blend_time = 0.25f;
    static constexpr float expression_blend_time = 0.2f;
    static constexpr float camera_distance = 3.2f;
    static constexpr float camera_pivot_height = 1.45f;
};
