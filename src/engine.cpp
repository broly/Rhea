#include "engine.h"

#include <chrono>
#include <set>

#include "framework/camera.h"
#include "framework/engine_clock.h"
#include "framework/world.h"
#include "platform/window.h"
#include "render/handle_types.h"
#include "render/render_graph.h"
#include "render/backends/vk/vk_render_backend.h"
#include "world_scripts/WorldScript_RotateAroundObject.h"


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
    
    renderer->init(window_handle, world);
    
    std::shared_ptr<EngineClock> clock = std::make_shared<EngineClock>();
    
    clock->start();
    
    
    
    world->set_clock(clock);
    world->add_script<WorldScript_RotateAroundObject>(); // TODO hardcoded
    
    world->init();
    

    while (!window_should_close(window)) {
        
        clock->tick();
        
        world->tick();
        
        platform::window::window_poll_events();
        renderer->execute();
        // render_graph->draw(*world->get_camera());
    }
    window_destroy(window);
}

void Engine::init_render_graph(RenderBackend& render_backend, RenderGraph& render_graph)
{
    

}
