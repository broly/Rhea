export module WorldScript_RotateAroundObject;

import framework;
import glm;
import <memory>;

export class WorldScript_RotateAroundObject : public WorldScript
{
public:
    void tick(double dt) override;
    
    bool rotation_started = false;
    glm::vec2 prev_mouse{};

    float yaw = 0.0f;
    float pitch = 0.0f;

    float mouse_sensitivity = 0.0025f;
    float move_speed = 10.0f;
    std::shared_ptr<RhActor> camera_actor = nullptr;

};
