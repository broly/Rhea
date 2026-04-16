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
import rail;

constexpr bool DO_NN_SAMPLES = false;

void WorldScript_VariousThings::tick(double dt)
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
    
    auto rail = world->find_actor_by_name<Rail>("rail");
    
    
    if (DO_NN_SAMPLES)
    {
        RhGlobals::engine->renderer->set_flag("reset_temporal_accum", true, false, true);
    }
    if (!do_once)
    {
        rail->add_on_tick("cam", [=] (const RailSampleData& d)
        {
            Transform newt = {d.position, d.rotation};
            camera_actor->set_transform(newt);
        });
        
        rail->add_on_tick("light", [=] (const RailSampleData& d)
        {
            Transform newt = {d.position, d.rotation};
            dir_light_actor->set_transform(newt);
            auto light = dir_light_actor->find_component<RhComp_Light>();
            light->color = d.color;
            light->update_scene_proxy();
        });
        if (DO_NN_SAMPLES)
        {
            rail->startup();
        }
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
    
        
        
        {
            yaw = 0.987127423;
            pitch = 0.20477125;
            t.position = glm::vec3{7.804476, 1.001801, 1.158290};
            t.rotation = glm::quat( 0.885217, 0.079783, -0.057427, -0.456434);
        }
        
        auto qrot = math::from_euler_rotation(glm::vec3(pitch, yaw, 0));
    
        t.rotation = qrot;
        
        camera_actor->set_transform(t);
        
        if (DO_NN_SAMPLES)
        {
            RhGlobals::engine->renderer->set_num_runs_per_frame(40);
        }
        // dir_light_actor->set_transform(t);
    }
    
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
    if (input->is_key_down(Key::J))
    {
        rail->startup();
        handled = true;
    }
    if (input->is_key_down(Key::B))
    {
        RhGlobals::engine->renderer->set_flag("reset_temporal_accum", true, false, true);
    }
    if (input->is_key_down(Key::Z))
    {
        if (first_cam_key_time == 0.0f)
        {
            first_cam_key_time = world->get_time_seconds();
        }
        const double time = world->get_time_seconds();
        if (time - last_cam_key_time < 1.0)
            return;
        last_cam_key_time = time;
        auto tl = camera_actor->get_transform();
        auto p = tl.position;
        auto r = tl.rotation;
        std::cout << "{\"timestamp_seconds\": " << time - first_cam_key_time << ",";
        std::cout << "\"position\": { \"x\": " << p.x << ", \"y\": " << p.y << ", \"z\": " << p.z << " } , ";
        std::cout << "\"rotation\": { \"x\": " << r.x << ", \"y\": " << r.y << ", \"z\": " << r.z << ", \"w\": " << r.w << " } },";
        std::cout << std::endl;
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
