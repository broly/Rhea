module;

#include <imgui.h>

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
import ecs;
import name;
import framework;
import character_controller;

constexpr bool DO_NN_SAMPLES = false;
constexpr bool DO_ANIMATE_LIGHT = true;

Transform WorldScript_VariousThings::get_camera_transform() const
{
    return scene::get_world_transform(world->registry, camera);
}

void WorldScript_VariousThings::set_camera_transform(const Transform& t)
{
    scene::set_world_transform(world->registry, camera, t);
}

CharacterAnimator* WorldScript_VariousThings::get_animator() const
{
    return character ? world->registry.get<CharacterAnimator>(character) : nullptr;
}

bool WorldScript_VariousThings::frame_character(Transform& t, bool face)
{
    const ecs::Entity target_entity = world->find_entity("cosmo_bunny");
    if (!target_entity)
        return false;

    const Transform ct = scene::get_world_transform(world->registry, target_entity);
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

    const Key keys[4] = { Key::F1, Key::F2, Key::F3, Key::F4 };
    bool pressed[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        const bool down = input->is_key_down(keys[i]);
        pressed[i] = down && !debug_view_keys_were_down[i];
        debug_view_keys_were_down[i] = down;
    }

    const DebugViewMode mode = cv_debug_view_mode.get();
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
        cv_debug_view_mode.set(new_mode);
        std::cout << "View mode: " << get_debug_view_mode_name(new_mode) << std::endl;
    }

    if (pressed[3])
    {
        cv_debug_show_skeleton.set(!cv_debug_show_skeleton.get());
        std::cout << "Skeleton overlay: " << (cv_debug_show_skeleton.get() ? "on" : "off") << std::endl;
    }

    // F9: dump the intermediate buffers of the next frame (GameRenderGraph "DebugDumpFrame")
    const bool dump_down = input->is_key_down(Key::F9);
    if (dump_down && !dump_key_was_down)
    {
        RhGlobals::engine->renderer->set_flag("debug_dump_frame", true, false, true);
        std::cout << "Frame dump requested: cache/debug_dump/<buffer>/frame_<N>.exr" << std::endl;
    }
    dump_key_was_down = dump_down;
}

void WorldScript_VariousThings::tick(double dt)
{
    tick_debug_views();

    ecs::Registry& registry = world->registry;

    if (!registry.alive(camera))
        camera = world->find_entity("viewer");
    if (!camera)
        return;

    if (!registry.alive(sun))
        sun = world->find_entity("dir_light0");
    if (!sun)
        return;

    if (!registry.alive(rail))
        rail = world->find_entity("rail");
    Rail* rail_track = registry.get<Rail>(rail);
    if (!rail_track)
        return;

    auto input = RhGlobals::engine->input;
    Transform t = get_camera_transform();

    static bool do_once = false;


    if (DO_NN_SAMPLES)
    {
        RhGlobals::engine->renderer->set_flag("reset_temporal_accum", true, false, true);
    }
    if (!do_once)
    {
        if (DO_NN_SAMPLES)
        {
            if (rail_track->timestep)
            {
                rail_track->accumulated_time = 499 * *rail_track->timestep;
                RhGlobals::engine->renderer->set_frame(499);
            }
            RhGlobals::engine->renderer->set_flag("do_readback_nn", true, false, false);
        }
        rail_track->on_tick["cam"] = [this] (const RailSampleData& d)
        {
            if (DO_NN_SAMPLES)
                set_camera_transform(Transform(d.position, d.rotation));
        };

        rail_track->on_tick["light"] = [this] (const RailSampleData& d)
        {
            ecs::Registry& registry = world->registry;
            scene::set_world_transform(registry, sun, Transform(d.position, d.rotation));
            glm::vec4 sun_color = d.color;
            // lighting presets tint the animated sun
            if (light_rig)
                sun_color = glm::vec4(glm::vec3(sun_color) * light_rig->get_sun_tint(), sun_color.w);
            if (Light* light = registry.get<Light>(sun))
                light->color = sun_color;
        };
        if (DO_NN_SAMPLES)
        {
            rail_track->start();
        }
        if (DO_ANIMATE_LIGHT)
        {
            rail_track->loop = true;
            rail_track->fixed_timestep = false;
            rail_track->time_dilation = 0.3;
            rail_track->start();
        }
        do_once = true;

        t.position = glm::vec3{0.0f, 30.0f, 10.0f};

        auto aabb = compute_world_bounds(registry);
        auto origin = t.position.glm();
        auto target = aabb.center();
        auto result = math::look_at(origin, target);
        t.rotation = result;
        set_camera_transform(t);
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

        set_camera_transform(t);

        if (DO_NN_SAMPLES)
        {
            RhGlobals::engine->renderer->set_num_runs_per_frame(40);
        }
    }

    // =========================
    // MOUSE
    // =========================
    glm::vec2 mouse{ input->mouse_x, input->mouse_y };

    // yaw / pitch edited in the debug UI
    bool handled = yaw != applied_yaw || pitch != applied_pitch;

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
        character = world->find_entity("cosmo_bunny");
        if (character && init_character(*world, character, "animations/cosmo_bunny/locomotion.json"))
        {
            CharacterAnimator& animator = *registry.get<CharacterAnimator>(character);
            const SkinnedMesh& mesh = *registry.get<SkinnedMesh>(character);
            animator.load_poses("animations/cosmo_bunny/poses.json", mesh.get_skeleton());
            animator.load_expressions("animations/cosmo_bunny/expressions.json", mesh);
            registry.add<PlayerControlled>(character);
            pitch = -0.25f;   // look slightly down on the character
        }
        else
            character = ecs::null_entity;
    }
    if (character && !registry.alive(character))
        character = ecs::null_entity;
    CharacterAnimator* animator = get_animator();

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
        if (!light_rig->init(*world, character, "cosmo_bunny_light_"))
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
        if (m_down && !sun_freeze_key_was_down)
            set_sun_frozen(!sun_frozen);
        sun_freeze_key_was_down = m_down;

        light_rig->tick(registry);
    }

    // T: next pose, Shift+T: previous (locomotion is part of the cycle), 0: back to locomotion
    // (LeftShift + 0/1/2 selects the tonemap output mode, see below)
    if (animator && animator->get_pose_count() > 0)
    {
        const bool t_down = input->is_key_down(Key::T);
        if (t_down && !pose_key_was_down)
        {
            const int32_t count = animator->get_pose_count() + 1;   // + locomotion
            const int32_t step = input->is_key_down(Key::LeftShift) ? count - 1 : 1;
            const int32_t current = animator->active_pose + 1;
            const int32_t next = (current + step) % count - 1;
            animator->select_pose(next);
        }
        pose_key_was_down = t_down;

        const bool zero_down = input->is_key_down(Key::_0) && !input->is_key_down(Key::LeftShift);
        if (zero_down && !locomotion_key_was_down)
            animator->select_pose(-1);
        locomotion_key_was_down = zero_down;
    }

    // N: next facial expression, Shift+N: previous (neutral is part of the cycle)
    if (animator && animator->get_expression_count() > 0)
    {
        const bool n_down = input->is_key_down(Key::N);
        if (n_down && !expression_key_was_down)
        {
            const int32_t count = animator->get_expression_count() + 1;   // + neutral
            const int32_t step = input->is_key_down(Key::LeftShift) ? count - 1 : 1;
            const int32_t current = animator->active_expression + 1;
            animator->select_expression((current + step) % count - 1);
        }
        expression_key_was_down = n_down;
    }

    // the character moves in the fixed ticks (character_controller), here only the camera follows it
    if (character)
    {
        if (PlayerControlled* player = registry.get<PlayerControlled>(character))
        {
            player->enabled = character_mode;
            player->camera_yaw = yaw;
        }
        if (character_mode)
        {
            pitch = glm::clamp(pitch, -1.2f, 0.5f);
            set_camera_transform(make_character_camera(*world, character, yaw, pitch));
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
        rail_track->start();
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
        auto tl = get_camera_transform();
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
        ecs::Query<const ReflectionCapture, const Name>(registry).each([&] (ecs::Entity e, const ReflectionCapture&, const Name& name) {
            gr->capture_ibl(scene::get_world_transform(registry, e).position.glm(), name);
        });
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
        if (Camera* camera_component = registry.get<Camera>(camera))
            camera_component->fov = glm::clamp(camera_component->fov * powf(0.9f, (float)scroll), glm::radians(10.0f), glm::radians(120.0f));
    }

    if (handled && free_camera)
        set_camera_transform(t);
    applied_yaw = yaw;
    applied_pitch = pitch;


}

void WorldScript_VariousThings::set_sun_frozen(bool frozen)
{
    Rail* rail_track = world->registry.get<Rail>(rail);
    if (!rail_track || frozen == sun_frozen)
        return;

    sun_frozen = frozen;
    if (sun_frozen)
    {
        sun_time_dilation = rail_track->time_dilation;
        rail_track->time_dilation = 0.0f;
    }
    else
        rail_track->time_dilation = sun_time_dilation;
}

void WorldScript_VariousThings::draw_debug_ui()
{
    if (world->registry.alive(camera))
    {
        ImGui::SeparatorText("Camera");
        for (bool face : {false, true})
        {
            if (face)
                ImGui::SameLine();
            if (ImGui::Button(face ? "Face close-up (V)" : "Frame character (F)"))
            {
                Transform t = get_camera_transform();
                if (frame_character(t, face))
                {
                    character_mode = false;
                    set_camera_transform(t);
                }
            }
        }
    }

    if (CharacterAnimator* character_animator = get_animator())
    {
        CharacterAnimator& animator = *character_animator;
        ImGui::SeparatorText("Character");

        // -1: locomotion / neutral face
        auto index_combo = [] (const char* label, int32_t count, int32_t active, const char* none_name,
                               auto&& get_name, auto&& select)
        {
            const std::string preview = active >= 0 ? get_name(active) : none_name;
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                for (int32_t index = -1; index < count; index++)
                {
                    const std::string item = index >= 0 ? get_name(index) : none_name;
                    if (ImGui::Selectable(item.c_str(), index == active))
                        select(index);
                }
                ImGui::EndCombo();
            }
        };

        index_combo("Pose (T)", animator.get_pose_count(), animator.active_pose, "Locomotion",
            [&] (int32_t i) { return animator.get_pose_name(i); },
            [&] (int32_t i) { animator.select_pose(i); });
        index_combo("Expression (N)", animator.get_expression_count(), animator.active_expression, "Neutral",
            [&] (int32_t i) { return animator.get_expression_name(i); },
            [&] (int32_t i) { animator.select_expression(i); });
    }

    if (light_rig)
    {
        ImGui::SeparatorText("Lighting");
        if (ImGui::BeginCombo("Preset (L)", light_rig->get_preset_name()))
        {
            for (size_t index = 0; index < light_rig->get_preset_count(); index++)
                if (ImGui::Selectable(light_rig->get_preset_name(index), index == light_rig->get_preset_index()))
                    light_rig->set_preset(index);
            ImGui::EndCombo();
        }

        bool frozen = sun_frozen;
        if (ImGui::Checkbox("Freeze sun (M)", &frozen))
            set_sun_frozen(frozen);
    }
}
