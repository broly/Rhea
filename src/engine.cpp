#include "engine.h"

#include <chrono>

#include "framework/camera.h"
#include "framework/engine_clock.h"
#include "framework/world.h"
#include "platform/window.h"
#include "render/handle_types.h"
#include "render/render_graph.h"
#include "render/backends/vk/vk_render_backend.h"
#include "world_scripts/WorldScript_RotateAroundObject.h"


void Engine::run()
{
    asset_manager = std::make_shared<AssetManager>();
    
    window_create(window, 1280, 720, "Rhea");
    
    RBWindowHandle window_handle = window.handle;
    
    std::unique_ptr<RenderBackend> render_backend = RenderBackend::create<VkRenderBackend>(window_handle);
    std::unique_ptr<RenderGraph> render_graph = std::make_unique<RenderGraph>();
    
    render_graph->add_pass({
        .name = "Geometry",
        .writes = { /* color */ },
        .execute = [&](RenderGraphContext& ctx)
        {
            auto pipeline = ctx.backend.get_pipeline_handle();
            ctx.backend.bind_pipeline(ctx.cmd, pipeline);

            auto camera_set = ctx.backend.get_camera_descriptor_set();
            ctx.backend.bind_descriptor_set(ctx.cmd, 0, camera_set);

            ctx.backend.draw(ctx.cmd, 36);
        }
    });
    // render_graph->initialize(window_handle);
    
    std::shared_ptr<EngineClock> clock = std::make_shared<EngineClock>();
    
    clock->start();
    
    std::shared_ptr<World> world = std::make_shared<World>();
    
    
    world->set_clock(clock);
    world->add_script<WorldScript_RotateAroundObject>(); // TODO hardcoded
    
    world->init();
    

    while (!window_should_close(window)) {
        
        clock->tick();
        
        world->tick();
        
        platform::window::window_poll_events();
        render_graph->execute(*render_backend);
        // render_graph->draw(*world->get_camera());
    }
    window_destroy(window);
}
