module WorldScript_RotateAroundObject;

import <iostream>;
import glm;
import rhmath;
import globals;
import input;
import profile;
import rhmath;
import rhcomponents;
import game;

void WorldScript_RotateAroundObject::tick(double dt)
{
    if (!camera_actor)
        camera_actor = world->find_actor_by_name("viewer");
    if (!camera_actor)
        return;
    
    
    if (!dir_light_actor)
        dir_light_actor = world->find_actor_by_name("dir_light0");
    if (!dir_light_actor)
        return;


    auto input = RhGlobals::engine->input;
    Transform t = camera_actor->get_transform();
    
    
    static bool do_once = false;
    
    if (!do_once)
    {
        do_once = true;
        
        t.position = glm::vec3{0.0f, 30.0f, 10.0f};
        
        auto aabb = world->get_world_aabb();
        auto origin = t.position.glm();
        auto target = aabb.center();
        auto result = math::look_at(origin, target);
        t.rotation = result;
        camera_actor->set_transform(t);
        glm::vec3 dir = glm::normalize(target - origin);
        pitch = asinf(glm::clamp(dir.y, -1.0f, 1.0f));
        yaw = atan2f(dir.x, -dir.z);
        auto qrot = math::from_euler_rotation(glm::vec3(pitch, yaw, 0));
    
        t.rotation = qrot;
        camera_actor->set_transform(t);
        // dir_light_actor->set_transform(t);
    }
    
    
    const double world_seconds = world->get_time_seconds();
    
    glm::quat dir_light_a = {-0.500154674, -0.28043431, -0.175259128, 0.800303757 };
    glm::quat a = {0.873911619, -0.195810378,0.434136629,0.0972735211 };
    glm::quat b = {0.710990787, -0.554269791, 0.342177004, 0.264937967};

    glm::vec4 color_a = {8, 8, 4, 1};
    glm::vec4 color_b = {10, 2, 0, 1};
    
    auto light = dir_light_actor->find_component<RhComp_Light>();
    auto light_transform = light->get_transform();
    
    
    
    float alpha = fmod(  world_seconds / 50, 1.0);
    
    glm::vec4 color_interp = glm::lerp(color_a, color_b, alpha);
    light->color = color_interp;
    light->update_scene_proxy();
    
    auto rot_interp =  glm::normalize(glm::lerp(b, a, alpha));
    light_transform.set_rotation(rot_interp);
    light->set_transform(light_transform);
    
    
    
    
    
    // =========================
    // MOUSE
    // =========================
    glm::vec2 mouse{ input->mouse_x, input->mouse_y };

    bool handled = false;
    
    auto aabb = world->get_world_aabb();
    auto origin = t.position.glm();
    auto target = aabb.center();
    
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
        handled = true;
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
    {
        t.position = t.position.glm() + t.forward() * speed;
        handled = true;
    }
    if (input->is_key_down(Key::S))
    {
        t.position = t.position.glm() - t.forward() * speed;
        handled = true;
    }
    if (input->is_key_down(Key::A))
    {
        t.position = t.position.glm() - t.right() * speed;
        handled = true;
    }
    if (input->is_key_down(Key::D))
    {
        t.position = t.position.glm() + t.right() * speed;
        handled = true;
    }
    if (input->is_key_down(Key::E))
    {
        t.position = t.position.glm() + t.up() * speed;
        handled = true;
    }
    if (input->is_key_down(Key::Q))
    {
        t.position = t.position.glm() - t.up() * speed;
        handled = true;
    }
    
    if (input->is_key_down(Key::G))
    {
        auto gr = dynamic_cast<GameRenderer*>(RhGlobals::engine->renderer.get());
        for (auto actor : world->get_actors())
        {
            if (auto comp = actor->find_component<RhComp_ReflectionCapture>())
            {
                gr->capture_ibl(actor->get_transform().position, actor->name);
            }
        }
        handled = true;
    }
    
    if (input->is_key_down(Key::H))
    {
        const double time = world->get_time_seconds();
        if (time - last_rg_switch_time < 1.0)
            return;
        last_rg_switch_time = time;
        RhGlobals::engine->renderer->toggle_flag("debug_shadow", true);
        handled = true;
    }
    
    if (handled)
    {
        camera_actor->set_transform(t);
        // dir_light_actor->set_transform(t);
    }
    
    
}
