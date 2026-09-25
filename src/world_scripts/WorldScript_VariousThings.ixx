export module WorldScript_RotateAroundObject;

import std.compat;

import framework;
import glm;
import rhmath;
import character_controller;
import character_light_rig;

export class WorldScript_VariousThings : public WorldScript
{
public:
    void tick(double dt) override;
    
    // Places the camera in front of the imported character (full body or face close-up).
    // Returns false when there is no such actor in the world.
    bool frame_character(Transform& camera_transform, bool face_close_up);
    
    // F1: lit, F2: wireframe (again: wireframe over lit), F3: next gbuffer channel (Shift+F3: previous),
    // F4: skeleton overlay
    void tick_debug_views();
    
    bool rotation_started = false;
    glm::vec2 prev_mouse{};

    float yaw = 0.0f;
    float pitch = 0.0f;

    float mouse_sensitivity = 0.0025f;
    float move_speed = 6.0f;
    std::shared_ptr<RhActor> camera_actor = nullptr;
    std::shared_ptr<RhActor> dir_light_actor = nullptr;
    double last_rg_switch_time = 0.f;
    double last_cam_key_time = 0.f;
    double first_cam_key_time = 0.f;
    
    std::unique_ptr<CharacterController> character;
    bool character_init_attempted = false;
    bool character_mode = true;
    bool character_toggle_was_down = false;
    bool pose_key_was_down = false;
    bool locomotion_key_was_down = false;
    bool expression_key_was_down = false;
    
    std::unique_ptr<CharacterLightRig> light_rig;
    bool light_rig_init_attempted = false;
    bool light_preset_key_was_down = false;
    bool sun_freeze_key_was_down = false;
    bool sun_frozen = false;
    float sun_time_dilation = 1.0f;
    
    // DebugViewMode (game module)
    uint32_t debug_view_mode = 0;
    uint32_t last_gbuffer_view_mode = 0;
    bool show_skeleton = false;
    bool debug_view_keys_were_down[4] = {};
};
