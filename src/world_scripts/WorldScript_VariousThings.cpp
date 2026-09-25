module WorldScript_RotateAroundObject;

import std.compat;

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
constexpr bool DO_ANIMATE_LIGHT = true;

bool WorldScript_VariousThings::frame_character(Transform& t, bool face)
{
    auto character = world->find_actor_by_name("cosmo_bunny");
    if (!character)
        return false;
    
    const Transform ct = character->get_transform();
    const glm::vec3 facing = ct.rotation.glm() * glm::vec3(0, 0, 1);   // glTF characters face +Z
    const glm::vec3 target = ct.position.glm() + glm::vec3(0, face ? 1.62f : 1.0f, 0);
    
    t.position = target + facing * (face ? 0.55f : 2.8f) + glm::vec3(0, face ? 0.02f : 0.15f, 0);
    
    // inverse of from_euler_rotation(pitch, yaw, 0): forward = (-sin(yaw)cos(p), sin(p), -cos(yaw)cos(p))
    const glm::vec3 dir = glm::normalize(target - t.position.glm());
    pitch = asinf(glm::clamp(dir.y, -1.0f, 1.0f));
    yaw = atan2f(-dir.x, -dir.z);
    t.rotation = math::from_euler_rotation(glm::vec3(pitch, yaw, 0));
    return true;
}

void WorldScript_VariousThings::tick_debug_views()
{
    auto input = RhGlobals::engine->input;
    auto& renderer = RhGlobals::engine->renderer;
    
    const Key keys[4] = { Key::F1, Key::F2, Key::F3, Key::F4 };
    bool pressed[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        const bool down = input->is_key_down(keys[i]);
        pressed[i] = down && !debug_view_keys_were_down[i];
        debug_view_keys_were_down[i] = down;
    }
    
    const DebugViewMode mode = (DebugViewMode)debug_view_mode;
    DebugViewMode new_mode = mode;

    if (pressed[0])
        new_mode = DebugViewMode::LIT;
    
    if (pressed[1])
        new_mode = mode == DebugViewMode::WIREFRAME ? DebugViewMode::LIT_WIREFRAME : DebugViewMode::WIREFRAME;
    
    if (pressed[2])
    {
        if (!is_gbuffer_view_mode(mode))
        {
            // back to the channel that was viewed last
            new_mode = is_gbuffer_view_mode((DebugViewMode)last_gbuffer_view_mode)
                ? (DebugViewMode)last_gbuffer_view_mode
                : first_gbuffer_view_mode;
        }
        else
        {
            const uint32_t first = (uint32_t)first_gbuffer_view_mode;
            const uint32_t count = (uint32_t)DebugViewMode::COUNT - first;
            const uint32_t step = input->is_key_down(Key::LeftShift) ? count - 1 : 1;
            new_mode = (DebugViewMode)(first + ((uint32_t)mode - first + step) % count);
        }
        last_gbuffer_view_mode = (uint32_t)new_mode;
    }
    
    if (new_mode != mode)
    {
        debug_view_mode = (uint32_t)new_mode;
        renderer->set_int_param(DebugViewParams::view_mode, (int)new_mode);
        std::cout << "View mode: " << get_debug_view_mode_name(new_mode) << std::endl;
    }
    
    if (pressed[3])
    {
        show_skeleton = !show_skeleton;
        renderer->set_int_param(DebugViewParams::show_skeleton, show_skeleton ? 1 : 0);
        std::cout << "Skeleton overlay: " << (show_skeleton ? "on" : "off") << std::endl;
    }
}

void WorldScript_VariousThings::tick(double dt)
{
    tick_debug_views();
    
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
        if (DO_NN_SAMPLES)
        {
            if (rail->timestep)
            {
                rail->set_accum_time(499 * *rail->timestep);
                RhGlobals::engine->renderer->set_frame(499);
            }
            RhGlobals::engine->renderer->set_flag("do_readback_nn", true, false, false);
        }
        rail->add_on_tick("cam", [=] (const RailSampleData& d)
        {
            if (DO_NN_SAMPLES)
            {
                Transform newt = {d.position, d.rotation};
                camera_actor->set_transform(newt);
            }
        });
        
        rail->add_on_tick("light", [=, this] (const RailSampleData& d)
        {
            Transform newt = {d.position, d.rotation};
            dir_light_actor->set_transform(newt);
            auto light = dir_light_actor->find_component<RhComp_Light>();
            glm::vec4 sun_color = d.color;
            // lighting presets tint the animated sun
            if (light_rig)
                sun_color = glm::vec4(glm::vec3(sun_color) * light_rig->get_sun_tint(), sun_color.w);
            light->color = sun_color;
            light->update_scene_proxy();
        });
        if (DO_NN_SAMPLES)
        {
            rail->startup();
        }
        if (DO_ANIMATE_LIGHT)
        {
            rail->loop = true;
            rail->fixed_timestep = false;
            rail->time_dilation = 0.3;
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
        
        // start looking at the imported character when it is in the level
        frame_character(t, false);
        
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
    
    // =========================
    // CHARACTER (third person): C toggles character control / free camera
    // =========================
    if (!character_init_attempted)
    {
        character_init_attempted = true;
        character = std::make_unique<CharacterController>();
        if (!character->init(world->find_actor_by_name("cosmo_bunny"), "animations/cosmo_bunny/locomotion.json"))
            character.reset();
        else
        {
            character->load_poses("animations/cosmo_bunny/poses.json");
            character->load_expressions("animations/cosmo_bunny/expressions.json");
            pitch = -0.25f;   // look slightly down on the character
        }
    }
    
    const bool toggle_down = input->is_key_down(Key::C);
    if (toggle_down && !character_toggle_was_down && character)
        character_mode = !character_mode;
    character_toggle_was_down = toggle_down;
    
    const bool free_camera = !character || !character_mode;
    
    // light rig: L next preset (LeftShift + L previous), M freezes / resumes the sun animation
    if (character && !light_rig_init_attempted)
    {
        light_rig_init_attempted = true;
        light_rig = std::make_unique<CharacterLightRig>();
        if (!light_rig->init(*world, world->find_actor_by_name("cosmo_bunny"), "cosmo_bunny_light_"))
            light_rig.reset();
    }
    if (light_rig)
    {
        const bool l_down = input->is_key_down(Key::L);
        if (l_down && !light_preset_key_was_down)
        {
            if (input->is_key_down(Key::LeftShift))
                light_rig->previous_preset();
            else
                light_rig->next_preset();
        }
        light_preset_key_was_down = l_down;
        
        const bool m_down = input->is_key_down(Key::M);
        if (m_down && !sun_freeze_key_was_down && rail)
        {
            sun_frozen = !sun_frozen;
            if (sun_frozen)
            {
                sun_time_dilation = rail->time_dilation;
                rail->time_dilation = 0.0f;
            }
            else
                rail->time_dilation = sun_time_dilation;
        }
        sun_freeze_key_was_down = m_down;
        
        light_rig->tick();
    }
    
    // T: next pose, Shift+T: previous (locomotion is part of the cycle), 0: back to locomotion
    // (LeftShift + 0/1/2 selects the tonemap output mode, see below)
    if (character && character->get_pose_count() > 0)
    {
        const bool t_down = input->is_key_down(Key::T);
        if (t_down && !pose_key_was_down)
        {
            const int32_t count = character->get_pose_count() + 1;   // + locomotion
            const int32_t step = input->is_key_down(Key::LeftShift) ? count - 1 : 1;
            const int32_t current = character->get_active_pose() + 1;
            const int32_t next = (current + step) % count - 1;
            character->select_pose(next);
        }
        pose_key_was_down = t_down;

        const bool zero_down = input->is_key_down(Key::_0) && !input->is_key_down(Key::LeftShift);
        if (zero_down && !locomotion_key_was_down)
            character->select_pose(-1);
        locomotion_key_was_down = zero_down;
    }

    // N: next facial expression, Shift+N: previous (neutral is part of the cycle)
    if (character && character->get_expression_count() > 0)
    {
        const bool n_down = input->is_key_down(Key::N);
        if (n_down && !expression_key_was_down)
        {
            const int32_t count = character->get_expression_count() + 1;   // + neutral
            const int32_t step = input->is_key_down(Key::LeftShift) ? count - 1 : 1;
            const int32_t current = character->get_active_expression() + 1;
            character->select_expression((current + step) % count - 1);
        }
        expression_key_was_down = n_down;
    }

    if (character)
    {
        character->tick((float)dt, character_mode ? input.get() : nullptr, yaw);
        
        if (character_mode)
        {
            pitch = glm::clamp(pitch, -1.2f, 0.5f);
            camera_actor->set_transform(character->make_camera_transform(yaw, pitch));
        }
    }
    
    auto qrot = math::from_euler_rotation(glm::vec3(pitch, yaw, 0));
    
    t.rotation = qrot;
    
    float speed = move_speed * (float)dt;
    
    
    // if (input->is_key_down(Key::P))
    // {
    //     prof::set_is_profiling(true);
    // }
    //
    // if (input->is_key_down(Key::O))
    // {
    //     prof::set_is_profiling(false);
    // }
    //
    //
    // if (input->is_key_down(Key::L))
    // {
    //     prof::dump();
    // }


    if (free_camera && input->is_key_down(Key::W))
    {
        t.position = t.position.glm() + t.forward() * speed;
        handled = true;
    }
    if (free_camera && input->is_key_down(Key::S))
    {
        t.position = t.position.glm() - t.forward() * speed;
        handled = true;
    }
    if (free_camera && input->is_key_down(Key::A))
    {
        t.position = t.position.glm() - t.right() * speed;
        handled = true;
    }
    if (free_camera && input->is_key_down(Key::D))
    {
        t.position = t.position.glm() + t.right() * speed;
        handled = true;
    }
    if (free_camera && input->is_key_down(Key::E))
    {
        t.position = t.position.glm() + t.up() * speed;
        handled = true;
    }
    if (free_camera && input->is_key_down(Key::Q))
    {
        t.position = t.position.glm() - t.up() * speed;
        handled = true;
    }
    if (input->is_key_down(Key::J))
    {
        rail->startup();
        handled = true;
    }
    if (input->is_key_down(Key::LeftShift) && input->is_key_down(Key::_2))
    {
        RhGlobals::engine->renderer->set_int_param("output_mode", 2);
        handled = true;
    }
    if (input->is_key_down(Key::LeftShift) && input->is_key_down(Key::_1))
    {
        RhGlobals::engine->renderer->set_int_param("output_mode", 1);
        handled = true;
    }
    if (input->is_key_down(Key::LeftShift) && input->is_key_down(Key::_0))
    {
        RhGlobals::engine->renderer->set_int_param("output_mode", 0);
        handled = true;
    }
    // F: frame the imported character (full body), V: face close-up
    if (input->is_key_down(Key::F) || input->is_key_down(Key::V))
    {
        if (frame_character(t, input->is_key_down(Key::V)))
            handled = true;
    }
    if (input->is_key_down(Key::B))
    {
        RhGlobals::engine->renderer->set_flag("reset_temporal_accum", true, false, true);
    }
    if (input->is_key_down(Key::R))
    {
        const double time = world->get_time_seconds();
        if (time - last_rg_switch_time < 1.0)
            return;
        last_rg_switch_time = time;
        
        
        RhGlobals::engine->render_hot_reload();
    }
    if (input->is_key_down(Key::K))
    {
        RhGlobals::engine->renderer->set_num_runs_per_frame(40);
        RhGlobals::engine->renderer->set_flag("reset_temporal_accum", true, false, true);
        RhGlobals::engine->renderer->set_flag("do_readback_nn", true, false, false);
        
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
    
    // mouse wheel: field of view of the free camera
    const double scroll = input->consume_scroll();
    if (free_camera && scroll != 0.0)
    {
        if (auto camera = camera_actor->find_component<RhComp_Camera>())
        {
            camera->fov = glm::clamp(camera->fov * powf(0.9f, (float)scroll), glm::radians(10.0f), glm::radians(120.0f));
            camera->update_scene_proxy();
            handled = true;   // set_transform below submits the proxy
        }
    }
    
    if (handled && free_camera)
    {
        camera_actor->set_transform(t);
        // dir_light_actor->set_transform(t);
    }
    
    
}
