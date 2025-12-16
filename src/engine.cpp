#include "engine.h"

#include <chrono>

#include "framework/camera.h"
#include "framework/engine_clock.h"
#include "framework/world.h"
#include "math/transform.h"
#include "platform/window.h"
#include "render/render_backend.h"
#include "render/backends/vk/vk_render_backend.h"
#include "world_scripts/WorldScript_RotateAroundObject.h"


void Engine::run()
{
    window_create(window, 1280, 720, "Rhea");
    
    auto render_backend = RenderBackend::create_backend<VkRenderBackend>();
    
    render_backend->init(window.handle);
    
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
        render_backend->draw_frame(*world->get_camera());
    }
    window_destroy(window);
}
