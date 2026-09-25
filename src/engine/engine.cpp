module engine;

import :engine;

import std.compat;


import platform;

import framework;
import render;
import vk;
import WorldScript_RotateAroundObject;
import profile;
import gpu_profile;
import ui;
import cvar;


void Engine::engine_init()
{
    // render_backend = RenderBackend::create<VkRenderBackend>(window_handle);
    // render_graph = std::make_unique<RenderGraph>();
    
}

void Engine::run()
{    
    // saved settings (cache/cvars.json), before anything reads them
    cvar::load();
    
    window_create(window, 1280, 720, "Rhea");
    
    window_handle = {window.handle};
    input = std::make_shared<Input>();
    
    platform::window::set_input(input.get());
    
    engine_init();
    
    
    
    world = std::make_shared<World>();
    scene_view = std::make_shared<SceneView>(world, renderer);
    
    renderer->init(window_handle);

    // GPU pass profiler: init now that the backend exists.
    // Toggle at runtime with P (start) / O (dump+stop) — see the loop below.
    gpuprof::init(renderer->get_backend().get());
    
    // debug UI: ` shows / hides it
    ui::init(window.handle);
    renderer->get_backend()->init_ui_overlay();
    
    std::shared_ptr<EngineClock> clock = std::make_shared<EngineClock>();
    
    clock->start();
    
    
    
    world->set_clock(clock);
    world->add_script<WorldScript_VariousThings>(); // TODO hardcoded
    
    world->init();    

    
    bool gpu_prof_prev_p = false;
    bool gpu_prof_prev_o = false;

    while (!window_should_close(window)) {
        prof::frame_start();
        
        platform::window::window_poll_events();
        
        renderer->get_backend()->ui_overlay_new_frame();
        ui::begin_frame();
        // the game does not see the mouse / keyboard while the UI uses them
        input->set_ui_capture(ui::wants_mouse(), ui::wants_keyboard());
        
        clock->tick();
        
        world->tick();
        
        if (ui::is_visible())
            debug_ui.draw(*this);
        ui::end_frame();
        
        scene_view->perform_extraction();

        // GPU profiler runtime control (edge-detected):
        //   P -> start timing (clears previous results)
        //   O -> dump to gpu_profiling_dump.txt and stop
        {
            const bool p_down = input->is_key_down(Key::P);
            const bool o_down = input->is_key_down(Key::O);
            if (p_down && !gpu_prof_prev_p)
            {
                gpuprof::clear_results();
                gpuprof::set_enabled(true);
            }
            if (o_down && !gpu_prof_prev_o)
            {
                gpuprof::dump_json();
                gpuprof::set_enabled(false);
            }
            gpu_prof_prev_p = p_down;
            gpu_prof_prev_o = o_down;
        }

        renderer->execute();
        prof::frame_end();
    }
    renderer->get_backend()->shutdown_ui_overlay();
    ui::shutdown();
    cvar::save();
    
    gpuprof::shutdown();
    window_destroy(window);
}

void Engine::render_hot_reload()
{
    renderer->hot_reload();
    scene_view->hot_reload();
}
