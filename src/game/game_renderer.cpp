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

    RBDescriptorSetLayout camera_layout = render_backend->create_descriptor_set_layout(camera_set);
    render_backend->allocate_descriptor_sets_for_layout(camera_layout, DescriptorPoolType::Frame);


    GraphicsPipelineDesc desc{
        .vertex_shader = "shaders/geometry.vert.spv",
        .fragment_shader = "shaders/geometry.frag.spv",
        .vertex_layout = VertexLayout::PositionNormalTangentUV,
        .layout = {
            .sets = { camera_layout }, // camera UBO
            .push_constants = {{
                .stages = ShaderStage::ss_Vertex,
                .offset = 0,
                .size = sizeof(glm::mat4),
            }}
        }
    };

    auto geometry_pipeline = render_backend->create_pipeline(desc);
    
    // HACK
    std::set<MeshHandle> registered_meshes;
    // ENDHACK


    render_graph->add_pass({
        .name = "Geometry",
        .writes = { RenderResource::SwapchainColor },
        .execute = [=](RenderGraphContext& ctx)
        {
            auto cmd = ctx.cmd;


            CameraUBO camera_ubo;
            camera_ubo.mvp = world->camera->projection(1.0) * world->camera->view();
            ctx.backend.update_descriptor_set_data(camera_layout, camera_ubo);

            ctx.backend.bind_pipeline(cmd, geometry_pipeline);

            // bind camera descriptor set (set 0)
            ctx.backend.bind_descriptor_set(
                cmd,
                0,
                ctx.backend.get_descriptor_set(camera_layout, DescriptorPoolType::Frame),
                geometry_pipeline
            );


            for (const auto& ro : world->get_render_extractor()->meshes)
            {
                // HACK
                if (!registered_meshes.contains(ro.mesh))
                {
                    ctx.backend.create_mesh_buffers(ro.mesh);
                }
                // ENDHACK
                ctx.backend.bind_mesh(cmd, ro.mesh);


                ctx.backend.push_constants(cmd, ro.world, geometry_pipeline);


                // ctx.backend.bind_descriptor_set(cmd, 1, ro.mesh.descriptor_set, geometry_pipeline);

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
