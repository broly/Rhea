module engine:engine;

import <chrono>;
import <set>;

import platform;

import framework;
import render;
import vk;
import WorldScript_RotateAroundObject;
import profile;


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
    
    std::shared_ptr<EngineClock> clock = std::make_shared<EngineClock>();
    
    clock->start();
    
    
    
    world->set_clock(clock);
    world->add_script<WorldScript_VariousThings>(); // TODO hardcoded
    
    world->init();    

    
    while (!window_should_close(window)) {
        prof::frame_start();
        
        clock->tick();
        
        world->tick();
        scene_view->perform_extraction();
        
        platform::window::window_poll_events();
        renderer->execute();
        prof::frame_end();
    }
    window_destroy(window);
}
