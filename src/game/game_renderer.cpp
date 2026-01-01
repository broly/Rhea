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
        }},
        .debug_name = "camera"
    };

    camera_layout = render_backend->create_descriptor_set_layout(camera_set);
    render_backend->allocate_descriptor_sets_for_layout(camera_layout, ResourceUsageType::Frame);
    camera_buffer = render_backend->create_uniform_buffer(sizeof(CameraUBO), ResourceUsageType::Frame);
    render_backend->bind_buffer_to_descriptor(camera_layout, 0, camera_buffer);
    
    DescriptorSetLayoutDesc material_set{
        .set_index = 1,
        .bindings = {
            {
                .binding = 0,
                .type = DescriptorType::UniformBuffer,
                .stages = ShaderStage::ss_Fragment
            },
            {
                .binding = 1,
                .type = DescriptorType::CombinedImageSampler,
                .stages = ShaderStage::ss_Fragment
            }
        },
        .debug_name = "material"
    };
    
    material_layout = render_backend->create_descriptor_set_layout(material_set);   
    
    
    GraphicsPipelineDesc geom_pipeline_desc{
        .vertex_shader = "shaders/geometry.vert.spv",
        .fragment_shader = "shaders/geometry.frag.spv",
        .vertex_layout = VertexLayout::PositionNormalTangentUV,
        .layout = {
            .sets = { 
                camera_layout,  // 0
                material_layout, // 1
                light_layout,
            }, 
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
    
    
    auto depth_sampler = render_backend->create_sampler({});
    
    
    
    
    RGTextureDesc swapchain_desc{
        .width  = 0,
        .height = 0,
        .format = render_backend->get_swapchain_format(),
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Present,
        .external = true
    };

    auto swapchain_color = render_graph->create_texture({
        .format   = render_backend->get_swapchain_format(),
        .usage    = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Present,
        .external = true
    });
    
    auto depth_texture = render_graph->create_texture({
        .format = TextureFormat::Depth24Stencil8,
        .usage  = RenderTextureUsage::DepthStencil
    });
    
    RGTextureDesc color_desc{
        .width  = 0,
        .height = 0,
        .format = TextureFormat::RGBA8_UNORM,
        .usage  = RenderTextureUsage::Type(RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled),
        .external = false
    };

    auto scene_color = render_graph->create_texture(color_desc);
    
    DescriptorSetLayoutDesc light_set{
        .set_index = 2,
        .bindings = {{
            .binding = 0,
            .type = DescriptorType::UniformBuffer,
            .stages = ShaderStage::ss_Fragment
        }},
        .debug_name = "light"
    };

    light_layout = render_backend->create_descriptor_set_layout(light_set);
    render_backend->allocate_descriptor_sets_for_layout(
        light_layout,
        ResourceUsageType::Frame
    );
    
    light_buffer = render_backend->create_uniform_buffer(
        sizeof(LightUBO),
        ResourceUsageType::Frame
    );
    
    render_backend->bind_buffer_to_descriptor(
        light_layout,
        0,
        light_buffer
    );
    
    render_graph->add_pass({
        .name = "GeometryForward",
        .writes = { swapchain_color, depth_texture },

        .execute = [=](RenderGraphContext& ctx)
        {
            auto& extractor = engine->scene_view;
            auto cmd = ctx.cmd;
            
            auto& camera_processor = extractor->get_processor<SceneViewProcessor_Camera>();

            // ---------- Camera ----------
            CameraUBO camera_ubo;
            camera_ubo.mvp =
                camera_processor.get_active_camera()->get_projection(1.0f) *
                camera_processor.get_active_camera()->view;

            ctx.backend.update_uniform_buffer(camera_buffer, camera_ubo);

            // ---------- Lights ----------
            LightUBO light_ubo{};
            light_ubo.light_count = 1;
            light_ubo.lights[0].position = { 0.0f, 3.0f, 10.0f };
            light_ubo.lights[0].color    = { 15.0f, 15.0f, 15.0f };

            ctx.backend.update_uniform_buffer(light_buffer, light_ubo);

            // ---------- Pipeline ----------
            ctx.backend.bind_pipeline(cmd, geometry_pipeline);

            ctx.backend.bind_descriptor_set(
                cmd, 0,
                ctx.backend.get_descriptor_set(camera_layout, ResourceUsageType::Frame),
                geometry_pipeline->get_pipeline_handle()
            );

            ctx.backend.bind_descriptor_set(
                cmd, 2,
                ctx.backend.get_descriptor_set(light_layout, ResourceUsageType::Frame),
                geometry_pipeline->get_pipeline_handle()
            );

            // ---------- Draw ----------
            
            auto& meshes_processor = extractor->get_processor<SceneViewProcessor_Mesh>();
            for (const auto& ro : meshes_processor.meshes)
            {
                const RenderMaterial& mat =
                    meshes_processor.get_or_create_material(ro.material.key);

                ctx.backend.get_or_create_mesh_buffers(ro.mesh);

                ctx.backend.bind_descriptor_set(
                    cmd, 1,
                    mat.descriptor,
                    geometry_pipeline->get_pipeline_handle()
                );

                ctx.backend.bind_mesh(cmd, ro.mesh);
                ctx.backend.push_constants(
                    cmd, ro.world,
                    geometry_pipeline->get_pipeline_handle()
                );

                ctx.backend.draw_indexed(
                    cmd,
                    ro.mesh.get().get_index_count()
                );
            }
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
