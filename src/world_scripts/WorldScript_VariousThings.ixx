export module WorldScript_RotateAroundObject;

import framework;
import glm;
import <memory>;

export class WorldScript_VariousThings : public WorldScript
{
public:
    void tick(double dt) override;
    
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
};
