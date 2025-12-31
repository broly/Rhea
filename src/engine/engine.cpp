module engine:engine;

import <chrono>;
import <set>;

import platform;

import framework;
import render;
import vk;
import WorldScript_RotateAroundObject;


void Engine::init()
{
    // render_backend = RenderBackend::create<VkRenderBackend>(window_handle);
    // render_graph = std::make_unique<RenderGraph>();
    
}

void Engine::run()
{
    asset_manager = std::make_shared<AssetManager>();
    
    window_create(window, 1280, 720, "Rhea");
    
    window_handle = {window.handle};
    
    init();
    
    world = std::make_shared<World>();
    scene_view = std::make_shared<SceneView>(world, renderer);
    
    renderer->init(window_handle);
    
    std::shared_ptr<EngineClock> clock = std::make_shared<EngineClock>();
    
    clock->start();
    
    
    
    world->set_clock(clock);
    world->add_script<WorldScript_RotateAroundObject>(); // TODO hardcoded
    
    world->init();
    scene_view->camera = world->camera;
    

    while (!window_should_close(window)) {
        
        clock->tick();
        
        world->tick();
        scene_view->perform_extraction();
        
        platform::window::window_poll_events();
        renderer->execute();
        // render_graph->draw(*world->get_camera());
    }
    window_destroy(window);
}

void Engine::init_render_graph(RenderBackend& render_backend, RenderGraph& render_graph)
{
    

}
