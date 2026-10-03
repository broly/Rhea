module engine;

import :engine;

import std.compat;


import platform;

import framework;
import render;
import vk;
import WorldScript_RotateAroundObject;
import rhcomponents;
import profile;
import gpu_profile;
import ui;
import cvar;
import audio;
import paths;

#include "profiling/profile.h"


void Engine::engine_init()
{
    // render_backend = RenderBackend::create<VkRenderBackend>(window_handle);
    // render_graph = std::make_unique<RenderGraph>();
    
}

void Engine::run()
{    
    // saved settings (cache/cvars.json), before anything reads them
    cvar::load();
    
    // RHEA_SOAK_SECONDS=<n>: unattended soak test for the blocky g-buffer emissive garbage (see
    // WorldScript_VariousThings / GenericRenderGraph::read_diag_queries): maximized window without stealing
    // the focus, face close-up, emissive watch summaries in the log, quits after n seconds of world time
    const char* soak_env = std::getenv("RHEA_SOAK_SECONDS");
    const double soak_seconds = soak_env ? std::atof(soak_env) : 0.0;
    // RHEA_BACKGROUND=1: the window opens without taking the keyboard focus (automation: RHEA_EXEC + screenshots)
    const char* background_env = std::getenv("RHEA_BACKGROUND");
    const bool background = background_env && background_env[0] == '1';
    // RHEA_WINDOW=maximized | <width>x<height>: the window of automated runs (benchmarks at the size the game is played at)
    int window_width = 1280;
    int window_height = 720;
    bool maximized = soak_seconds > 0.0;
    if (const char* window_env = std::getenv("RHEA_WINDOW"))
    {
        int width = 0;
        int height = 0;
        if (std::string_view(window_env) == "maximized")
            maximized = true;
        else if (std::sscanf(window_env, "%dx%d", &width, &height) == 2 && width > 0 && height > 0)
        {
            window_width = width;
            window_height = height;
        }
    }
    window_create(window, window_width, window_height, "Rhea", {
        .maximized = maximized,
        .focus_on_show = soak_seconds <= 0.0 && !background,
    });

    // RHEA_RENDERDOC=1: RenderDoc in-app API (before the Vulkan instance exists), captures go to cache/renderdoc.
    // The emissive watch triggers captures of the corrupted frames (GenericRenderGraph::read_diag_queries)
    if (const char* renderdoc_env = std::getenv("RHEA_RENDERDOC"); renderdoc_env && renderdoc_env[0] == '1')
    {
        std::string renderdoc_error;
        if (platform::renderdoc::load(&renderdoc_error))
        {
            const auto capture_dir = paths::get_cache_path() / "renderdoc";
            std::filesystem::create_directories(capture_dir);
            platform::renderdoc::set_capture_path_template((capture_dir / "rhea").string());
            std::printf("RenderDoc loaded, captures: %s\n", capture_dir.string().c_str());
        }
        else
            std::printf("RenderDoc requested (RHEA_RENDERDOC=1) but not loaded: %s\n", renderdoc_error.c_str());
    }
    
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
    
    // debug UI: Shift+` shows / hides it, ` opens the console
    ui::init(window.handle);
    renderer->get_backend()->init_ui_overlay();
    debug_ui.register_console_commands(*this);
    
    std::shared_ptr<EngineClock> clock = std::make_shared<EngineClock>();
    
    clock->start();
    
    
    
    world->set_clock(clock);

    // engine services for systems and hooks (component types and systems register themselves)
    world->registry.set_resource_ref(*scene_view);          // ResMut<SceneView>
    world->registry.set_resource_ref(std::as_const(*input)); // Res<Input>

    // backend from the cvar audio.backend; silence (null backend) when no device can be opened
    audio_engine = std::make_unique<audio::AudioEngine>();
    audio_engine->init();
    world->registry.set_resource_ref(*audio_engine);        // ResMut<audio::AudioEngine>

    world->add_script<WorldScript_VariousThings>(); // TODO hardcoded
    
    world->init();    

    
    bool gpu_prof_prev_p = false;
    bool gpu_prof_prev_o = false;

    while (!window_should_close(window)) {
        prof::frame_start();
        
        {
            PROFILE("poll_events");
            platform::window::window_poll_events();
        }
        
        renderer->get_backend()->ui_overlay_new_frame();
        ui::begin_frame();
        // the game does not see the mouse / keyboard while the UI uses them
        input->set_ui_capture(ui::wants_mouse(), ui::wants_keyboard());
        
        clock->tick();
        
        {
            PROFILE("World::tick");
            world->tick();
        }
        {
            // after Late (the listener follows the camera): bus volumes, finished voices
            PROFILE("audio");
            audio_engine->update();
        }
        // RHEA_SOAK_CAP_UNTIL=<s>: soak runs hold ~30 FPS until then, to move the full-load step in time
        static const char* soak_cap_env = std::getenv("RHEA_SOAK_CAP_UNTIL");
        static const double soak_cap_until = soak_cap_env ? std::atof(soak_cap_env) : 0.0;
        if (soak_seconds > 0.0 && world->get_time_seconds() < soak_cap_until)
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        if (soak_seconds > 0.0 && world->get_time_seconds() > soak_seconds)
        {
            std::printf("Soak test finished after %.0f s\n", world->get_time_seconds());
            platform::window::window_request_close(window);
        }
        debug_ui.draw_world_debug(*this);
        
        {
            PROFILE("debug_ui");
            if (ui::is_visible())
                debug_ui.draw(*this);
            ui::end_frame();
        }
        
        {
            PROFILE("SceneView::perform_extraction");
            scene_view->perform_extraction();
        }

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

        {
            PROFILE("Renderer::execute");
            renderer->execute();
        }
        prof::frame_end();
    }
    audio_engine->shutdown();
    renderer->get_backend()->shutdown_ui_overlay();
    ui::shutdown();
    // soak runs override settings (RHEA_SOAK_PROBES...), automated runs set cvars from RHEA_EXEC (debug logs, AI
    // off, hidden UI): none of it must end up in the user's cvars.json
    if (soak_seconds <= 0.0 && !std::getenv("RHEA_EXEC"))
        cvar::save();
    
    gpuprof::shutdown();
    window_destroy(window);
}

void Engine::render_hot_reload()
{
    renderer->hot_reload();
    scene_view->hot_reload();
}
