module engine:engine;

import <chrono>;
import <set>;

import platform;

import framework;
import render;
import vk;
import WorldScript_RotateAroundObject;
import profile;
import gpu_profile;


void Engine::engine_init()
{
    // render_backend = RenderBackend::create<VkRenderBackend>(window_handle);
    // render_graph = std::make_unique<RenderGraph>();
    
}

void Engine::run()
{    
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
    
    std::shared_ptr<EngineClock> clock = std::make_shared<EngineClock>();
    
    clock->start();
    
    
    
    world->set_clock(clock);
    world->add_script<WorldScript_VariousThings>(); // TODO hardcoded
    
    world->init();    

    
    bool gpu_prof_prev_p = false;
    bool gpu_prof_prev_o = false;

    while (!window_should_close(window)) {
        prof::frame_start();
        
        clock->tick();
        
        world->tick();
        scene_view->perform_extraction();
        
        platform::window::window_poll_events();

        // GPU profiler runtime control (edge-detected):
        //   P -> start timing (clears previous results)
        //   O -> dump to gpu_profiling_dump.txt and stop
        // {
        //     const bool p_down = input->is_key_down(Key::P);
        //     const bool o_down = input->is_key_down(Key::O);
        //     if (p_down && !gpu_prof_prev_p)
        //     {
        //         gpuprof::clear_results();
        //         gpuprof::set_enabled(true);
        //     }
        //     if (o_down && !gpu_prof_prev_o)
        //     {
        //         gpuprof::dump();
        //         gpuprof::set_enabled(false);
        //     }
        //     gpu_prof_prev_p = p_down;
        //     gpu_prof_prev_o = o_down;
        // }

        renderer->execute();
        prof::frame_end();
    }
    gpuprof::shutdown();
    window_destroy(window);
}

void Engine::render_hot_reload()
{
    renderer->hot_reload();
    scene_view->hot_reload();
}
