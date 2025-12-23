#include "game_renderer.h"

#include <set>

#include "framework/world.h"
#include "render/backends/vk/vk_render_backend.h"

void GameRenderer::init(RBWindowHandle in_window, std::shared_ptr<World> in_world)
{
    Renderer::init(in_window, in_world);
    
    render_backend = RenderBackend::create<VkRenderBackend>(in_window);
    render_graph = std::make_shared<RenderGraph>();
    
    DescriptorSetLayoutDesc camera_set{
        .set_index = 0,
        .bindings = {{
            .binding = 0,
            .type = DescriptorType::UniformBuffer,
            .stages = ShaderStage::ss_Vertex
        }}
    };

    camera_layout = render_backend->create_descriptor_set_layout(camera_set);
    render_backend->allocate_descriptor_sets_for_layout(camera_layout, ResourceUsageType::Frame);
    camera_buffer = render_backend->create_uniform_buffer(sizeof(CameraUBO), ResourceUsageType::Frame);
    render_backend->bind_buffer_to_descriptor(camera_layout, 0, camera_buffer);

    GraphicsPipelineDesc geom_pipeline_desc{
        .vertex_shader = "shaders/geometry.vert.spv",
        .fragment_shader = "shaders/geometry.frag.spv",
        .vertex_layout = VertexLayout::PositionNormalTangentUV,
        .layout = {
            .sets = { camera_layout }, 
            .push_constants = {{
                .stages = ShaderStage::ss_Vertex,
                .offset = 0,
                .size = sizeof(glm::mat4),
            }}
        }
    };

    auto geometry_pipeline = render_backend->create_pipeline(geom_pipeline_desc);
    
    RGTextureDesc swapchain_desc{
        .width  = 0,
        .height = 0,
        .format = render_backend->get_swapchain_format(),
        .usage  = RGTextureUsage::ColorAttachment | RGTextureUsage::Present,
        .external = true
    };

    auto swapchain_color = render_graph->create_texture(swapchain_desc);
    
    render_graph->add_pass({
        .name = "Geometry",
        .writes = { swapchain_color },
        .execute = [=](RenderGraphContext& ctx)
        {
            auto cmd = ctx.cmd;

            CameraUBO camera_ubo;
            camera_ubo.mvp = world->camera->projection(1.0) * world->camera->view();
            ctx.backend.update_uniform_buffer(camera_buffer, camera_ubo);

            ctx.backend.bind_pipeline(cmd, geometry_pipeline);

            ctx.backend.bind_descriptor_set(
                cmd,
                0,
                ctx.backend.get_descriptor_set(camera_layout, ResourceUsageType::Frame),
                geometry_pipeline
            );

            for (const auto& ro : world->get_render_extractor()->meshes)
            {
                ctx.backend.get_or_create_mesh_buffers(ro.mesh);

                ctx.backend.bind_mesh(cmd, ro.mesh);

                ctx.backend.push_constants(cmd, ro.world, geometry_pipeline);

                ctx.backend.draw_indexed(cmd, ro.mesh.get().get_index_count());
            }

        }
    });

    render_graph->compile();
}

void GameRenderer::execute()
{
    render_graph->execute(*render_backend);
}
