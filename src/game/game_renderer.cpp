module game:renderer;

import render;
import vk;
import glm;

void GameRenderer::set_engine(const std::shared_ptr<Engine>& in_engine)
{
    engine = in_engine;
}

void GameRenderer::init(RBWindowHandle in_window)
{
    Renderer::init(in_window);
    
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
    
    DescriptorSetLayoutDesc material_set{
        .set_index = 1,
        .bindings = {{
            .binding = 0,
            .type = DescriptorType::CombinedImageSampler,
            .stages = ShaderStage::ss_Fragment
        }}
    };

    // auto material_layout = render_backend->create_descriptor_set_layout(material_set);
    // render_backend->allocate_descriptor_sets_for_layout(material_layout, ResourceUsageType::Persistent);  // todo: replace Persistent -> Static

    GraphicsPipelineDesc geom_pipeline_desc{
        .vertex_shader = "shaders/geometry.vert.spv",
        .fragment_shader = "shaders/geometry.frag.spv",
        .vertex_layout = VertexLayout::PositionNormalTangentUV,
        .layout = {
            .sets = { camera_layout },//, material_layout }, 
            .push_constants = {{
                .stages = ShaderStage::ss_Vertex,
                .offset = 0,
                .size = sizeof(glm::mat4),
            }}
        },
        .rt_compat = {
            .color_attachment_count = 1,
            .has_depth = true
        }
    };

    auto geometry_pipeline = render_backend->create_pipeline(geom_pipeline_desc);
    
    
    
    DescriptorSetLayoutDesc lighting_set{
        .set_index = 0,
        .bindings = {{
            .binding = 0,
            .type = DescriptorType::CombinedImageSampler,
            .stages = ShaderStage::ss_Fragment
        }}
    };
    
    auto depth_sampler = render_backend->create_sampler({});
    
    auto lighting_layout = render_backend->create_descriptor_set_layout(lighting_set);
    render_backend->allocate_descriptor_sets_for_layout(lighting_layout, ResourceUsageType::Frame);
    
    
    GraphicsPipelineDesc lighting_pipeline_desc{
        .vertex_shader   = "shaders/fullscreen.vert.spv",
        .fragment_shader = "shaders/lighting.frag.spv",
        .vertex_layout = VertexLayout::None,

        .layout = {
            .sets = { lighting_layout },
            .push_constants = {}
        },

        .rt_compat = {
            .color_attachment_count = 1,
            .has_depth = false
        }
    };

    auto lighting_pipeline = render_backend->create_pipeline(lighting_pipeline_desc);
    
    
    RGTextureDesc swapchain_desc{
        .width  = 0,
        .height = 0,
        .format = render_backend->get_swapchain_format(),
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Present,
        .external = true
    };

    auto swapchain_color = render_graph->create_texture(swapchain_desc);
    
    
    
    RGTextureDesc depth_desc{
        .width  = 0,
        .height = 0,
        .format = TextureFormat::Depth24Stencil8,
        .usage = RenderTextureUsage::DepthStencil | RenderTextureUsage::Sampled,
        .external = false
    };

    RGTextureHandle depth_texture = render_graph->create_texture(depth_desc);
    
    RGTextureDesc color_desc{
        .width  = 0,
        .height = 0,
        .format = TextureFormat::RGBA8_UNORM,
        .usage  = RenderTextureUsage::Type(RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled),
        .external = false
    };

    auto scene_color = render_graph->create_texture(color_desc);
    
    render_graph->add_pass({
        .name = "Geometry",
        .writes = { scene_color, depth_texture },
        .execute = [=](RenderGraphContext& ctx)
        {
            auto& extractor = engine->scene_extractor;
            auto cmd = ctx.cmd;
            
            CameraUBO camera_ubo;
            
            camera_ubo.mvp = extractor->camera->projection(1.0) * extractor->camera->view();
            ctx.backend.update_uniform_buffer(camera_buffer, camera_ubo);

            ctx.backend.bind_pipeline(cmd, geometry_pipeline);

            ctx.backend.bind_descriptor_set(
                cmd,
                0,
                ctx.backend.get_descriptor_set(camera_layout, ResourceUsageType::Frame),
                geometry_pipeline->get_pipeline_handle()
            );
            
            // ctx.backend.bind_descriptor_set(
            //     cmd,
            //     1,
            //     ctx.backend.get_descriptor_set(material_layout, ResourceUsageType::Persistent),  // todo: replace Persistent -> Static
            //     geometry_pipeline->get_pipeline_handle()
            // );

            for (const auto& ro : extractor->meshes)
            {
                ctx.backend.get_or_create_mesh_buffers(ro.mesh);

                ctx.backend.bind_mesh(cmd, ro.mesh);

                ctx.backend.push_constants(cmd, ro.world, geometry_pipeline->get_pipeline_handle());

                ctx.backend.draw_indexed(cmd, ro.mesh.get().get_index_count());
            }

        }
    });
    
    render_graph->add_pass({
        .name = "Lighting",
        .reads = { depth_texture },
        .writes = { swapchain_color },
        .descriptor_layout = lighting_layout,
        .descriptor_set = render_backend->get_descriptor_set(
            lighting_layout,
            ResourceUsageType::Frame
        ),
        .execute = [=](RenderGraphContext& ctx)
        {
            auto cmd = ctx.cmd;
            
            ctx.bind_sampled_texture(
                lighting_layout,
                0,              // binding = 0
                depth_texture
            );

            ctx.backend.bind_pipeline(cmd, lighting_pipeline);

            ctx.backend.bind_descriptor_set(
                cmd,
                0,
                ctx.backend.get_descriptor_set(lighting_layout, ResourceUsageType::Frame),
                lighting_pipeline->get_pipeline_handle()
            );

            ctx.backend.draw_fullscreen(cmd);
        }
    });

    render_graph->compile(*render_backend);
}

void GameRenderer::execute()
{
    auto& backend = *render_backend;
    
    RBFrameHandle frame = backend.get_current_frame();

    backend.wait_for_frame(frame);

    backend.reset_frame_fence(frame);

    if (!backend.acquire_next_image(frame))
        return;

    RBCommandList cmd = backend.begin_commands(frame);
    render_graph->execute(backend, cmd, frame);
    backend.end_commands(cmd);

    backend.submit_frame(frame, cmd);

    backend.advance_frame();

}
