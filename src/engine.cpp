#include "engine.h"

#include <chrono>

#include "framework/camera.h"
#include "framework/engine_clock.h"
#include "framework/world.h"
#include "platform/window.h"
#include "render/handle_types.h"
#include "render/render_graph.h"
#include "render/scene_extractor.h"
#include "render/backends/vk/vk_camera_ubo.h"
#include "render/backends/vk/vk_render_backend.h"
#include "world_scripts/WorldScript_RotateAroundObject.h"


void Engine::run()
{
    asset_manager = std::make_shared<AssetManager>();
    
    window_create(window, 1280, 720, "Rhea");
    
    RBWindowHandle window_handle = window.handle;
    
    std::unique_ptr<RenderBackend> render_backend = RenderBackend::create<VkRenderBackend>(window_handle);
    std::unique_ptr<RenderGraph> render_graph = std::make_unique<RenderGraph>();
    
    
    std::shared_ptr<World> world = std::make_shared<World>();
    
    DescriptorSetLayoutDesc camera_set{
        .set_index = 0,
        .bindings = {
            {
                .binding = 0,
                .type = DescriptorType::UniformBuffer,
                .stages = ShaderStage::Vertex
            }
        }
    };
    
    RBDescriptorSetLayout camera_layout = render_backend->create_descriptor_set_layout(camera_set);
    render_backend->allocate_descriptor_sets_for_layout(camera_layout, DescriptorPoolType::Frame);
    
    GraphicsPipelineDesc desc{
        .vertex_shader = "geometry.vert",
        .fragment_shader = "geometry.frag",
        .vertex_layout = VertexLayout::PositionNormalTangentUV,
        .layout = {
            .sets = { camera_layout },
            .push_constants = {{
                    .stages = ShaderStage::Vertex,
                    .offset = 0,
                    .size = sizeof(glm::mat4),
            }}
        }
    };
    
    auto geometry_pipeline = render_backend->create_pipeline(desc);
    
    render_graph->add_pass({
        .name = "Geometry",
        .writes = { /* color */ },
        .execute = [&](RenderGraphContext& ctx)
        {
            auto cmd = ctx.cmd;
            
            CameraUBO camera_ubo;
            camera_ubo.mvp = world->camera->projection(1.0) * world->camera->view();

            ctx.backend.update_descriptor_set_data(camera_layout, camera_ubo);
            
            ctx.backend.begin_render_pass(cmd, ctx.framebuffer);
            ctx.backend.bind_pipeline(cmd, geometry_pipeline);
            ctx.backend.bind_descriptor_set(
                cmd,
                0,
                ctx.backend.get_descriptor_set(camera_layout, DescriptorPoolType::Frame),
                geometry_pipeline
            );

            for (const auto& ro : world->get_render_extractor()->meshes)
            {
                ctx.backend.bind_mesh(cmd, ro.mesh);
                ctx.backend.push_constants(cmd, ro.world);
                ctx.backend.draw_indexed(cmd, ro.mesh.get().get_index_count());
            }

            ctx.backend.end_render_pass(cmd);
        }
    });
    render_graph->compile();
    // render_graph->initialize(window_handle);
    
    std::shared_ptr<EngineClock> clock = std::make_shared<EngineClock>();
    
    clock->start();
    
    
    
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
