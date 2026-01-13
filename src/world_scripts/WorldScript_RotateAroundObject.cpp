module WorldScript_RotateAroundObject;

import <iostream>;
#include <math.h>
import glm;
import rhmath;
import globals;
import input;
import profile;

void WorldScript_RotateAroundObject::tick(double dt)
{
    if (!camera_actor)
        camera_actor = world->find_actor_by_name("viewer");
    if (!camera_actor)
        return;

    auto input = RhGlobals::engine->input;
    Transform t = camera_actor->get_transform();
    
    static bool do_once = false;
    
    if (!do_once)
    {
        do_once = true;
        
        t.position = glm::vec3{0.0f, 0.0f, 10.0f};
        camera_actor->set_transform(t);
    }

    // =========================
    // MOUSE
    // =========================
    glm::vec2 mouse{ input->mouse_x, input->mouse_y };

    if (input->is_key_down(Key::MouseLeft))
    {
        if (!rotation_started)
        {
            rotation_started = true;
            prev_mouse = mouse;
        }

        glm::vec2 delta = mouse - prev_mouse;
        prev_mouse = mouse;

        yaw   += delta.x * mouse_sensitivity;
        pitch += delta.y * mouse_sensitivity;

        pitch = glm::clamp(
            pitch,
            -glm::radians(89.0f),
             glm::radians(89.0f)
        );
    }
    else
    {
        rotation_started = false;
    }

    auto qrot = math::from_euler_rotation(glm::vec3(pitch, yaw, 0));
    
    t.rotation = qrot;
    
    float speed = move_speed * (float)dt;
    
    if (input->is_key_down(Key::P))
    {
        prof::set_is_profiling(true);
    }
    
    if (input->is_key_down(Key::O))
    {
        prof::set_is_profiling(false);
    }
    
    if (input->is_key_down(Key::L))
    {
        prof::dump();
    }


    if (input->is_key_down(Key::W))
        t.position = t.position.glm() + t.forward() * speed;
    if (input->is_key_down(Key::S))
        t.position = t.position.glm() - t.forward() * speed;
    if (input->is_key_down(Key::A))
        t.position = t.position.glm() - t.right() * speed;
    if (input->is_key_down(Key::D))
        t.position = t.position.glm() + t.right() * speed;
    if (input->is_key_down(Key::E))
        t.position = t.position.glm() + t.up() * speed;
    if (input->is_key_down(Key::Q))
        t.position = t.position.glm() - t.up() * speed;

    
    // std::cout << t.position.x << " " << t.position.y << " " << t.position.z << std::endl;
    camera_actor->set_transform(t);
}
