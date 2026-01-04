module game:renderer;

import render;
import vk;
import glm;
import rhmath;
import <vector>;

// geometry

// locations
#define VERT_LOCATION_POSTION 0
#define VERT_LOCATION_NORMAL 1
#define VERT_LOCATION_UV 2
#define VERT_LOCATION_TANGENT 3

// geometry.vert / geometry.frag

#define SET_CAMERA 0
    #define BINDING_CAMERA_UBO 0

#define SET_MATERIAL 1
    #define BINDING_MATERIAL_UBO 0
    #define BINDING_BASE_COLOR 1
    #define BINDING_EMISSIVE 2
    #define BINDING_NORMAL_MAP 3
    #define BINDING_ORM 4

#define SET_LIGHT 2
    #define BINDING_LIGHT_UBO 0

// tonemap.frag

#define SET_TONEMAPPING 0
    #define BINDING_HDR_COLOR 0
#include "common/assertion_macros.h"

void GameRenderer::set_engine(const std::shared_ptr<Engine>& in_engine)
{
    engine = in_engine;
}

void GameRenderer::init(RBWindowHandle in_window)
{
    Renderer::init(in_window);
    
    render_backend = RenderBackend::create<VkRenderBackend>(in_window);
    render_graph = std::make_shared<RenderGraph>(render_backend);
    
    DescriptorSetLayoutDesc camera_set{
        .set_index = SET_CAMERA,
        .bindings = {{
            .binding = BINDING_CAMERA_UBO,
            .type = DescriptorType::UniformBuffer,
            .stages = ShaderStage::Vertex | ShaderStage::Fragment
        }},
        .debug_name = "camera"
    };

    camera_layout = render_backend->create_descriptor_set_layout(camera_set);
    render_backend->allocate_descriptor_sets_for_layout(camera_layout, ResourceUsageType::Frame);
    camera_buffer = render_backend->create_uniform_buffer(sizeof(CameraUBO), ResourceUsageType::Frame);
    render_backend->bind_buffer_to_descriptor(camera_layout, BINDING_CAMERA_UBO, camera_buffer);
    
    RGTextureDesc hdr_color_desc{
        .width  = 0,
        .height = 0,
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
        .external = false
    };

    auto hdr_color = render_graph->create_texture(hdr_color_desc);
    
    DescriptorSetLayoutDesc material_set{
        .set_index = SET_MATERIAL,
        .bindings = {
            {
                .binding = BINDING_MATERIAL_UBO,
                .type = DescriptorType::UniformBuffer,
                .stages = ShaderStage::Fragment
            },
            {
                .binding = BINDING_BASE_COLOR,
                .type = DescriptorType::CombinedImageSampler,
                .stages = ShaderStage::Fragment
            },
            {
                .binding = BINDING_EMISSIVE,
                .type = DescriptorType::CombinedImageSampler,
                .stages = ShaderStage::Fragment
            },
            {
                .binding = BINDING_NORMAL_MAP,
                .type = DescriptorType::CombinedImageSampler,
                .stages = ShaderStage::Fragment
            },
            {
                .binding = BINDING_ORM,
                .type = DescriptorType::CombinedImageSampler,
                .stages = ShaderStage::Fragment
            }
        },
        .debug_name = "material"
    };
    
    material_layout = render_backend->create_descriptor_set_layout(material_set);   
    
    
    DescriptorSetLayoutDesc light_set{
        .set_index = SET_LIGHT,
        .bindings = {{
            .binding = BINDING_LIGHT_UBO,
            .type = DescriptorType::UniformBuffer,
            .stages = ShaderStage::Fragment
        }},
        .debug_name = "light"
    };

    
    light_layout = render_backend->create_descriptor_set_layout(light_set);
    
    
    GraphicsPipelineDesc geom_pipeline_desc{
        .stages = {
            {
                .stage = ShaderStage::Vertex,
                .shader = "shaders/geometry.vert.spv",
                .vertex_layouts = {
                    VertexLayoutData {
                            .binding_index = 0,
                            .stride = sizeof(Vertex),
                            .attributes = {
                                { "in_position", offsetof(Vertex, position) },
                                { "in_normal", offsetof(Vertex, normal) },
                                { "in_uv", offsetof(Vertex, tex_coord) },
                                { "in_tangent", offsetof(Vertex, tangent) }
                            }
                        }
                    }
            },
            {
                .stage = ShaderStage::Fragment,
                .shader = "shaders/geometry.frag.spv"
            }
        },
        .layout = {
            .sets = { 
                camera_layout,  // 0
                material_layout, // 1
                light_layout, // 2
            }, 
            .push_constants = {{
                .stages = ShaderStage::Vertex,
                .offset = 0,
                .size = sizeof(glm::mat4),
            }}
        },
        .rt_compat = {
            .color_attachment_count = 1,
            .has_depth = true
        }
    };
    
    
    DescriptorSetLayoutDesc tonemap_set{
        .set_index = SET_TONEMAPPING,
        .bindings = {{
            .binding = BINDING_HDR_COLOR,
            .type    = DescriptorType::CombinedImageSampler,
            .stages  = ShaderStage::Fragment
        }},
        .debug_name = "tonemap"
    };
    
    tonemap_layout = render_backend->create_descriptor_set_layout(tonemap_set);   
    
    GraphicsPipelineDesc tonemap_pipeline_desc{
        .stages = {
            {
                .stage = ShaderStage::Vertex,
                .shader = "shaders/fullscreen.vert.spv",
            },
            {
                .stage = ShaderStage::Fragment,
                .shader = "shaders/tonemap.frag.spv",
            }
        },
        .layout = {
            .sets = { tonemap_layout },
        },
        .rt_compat = {
            .color_attachment_count = 1,
            .has_depth = false
        }
    };
    auto tonemap_pipeline = render_backend->create_pipeline(tonemap_pipeline_desc);

    auto geometry_pipeline = render_backend->create_pipeline(geom_pipeline_desc);
    
    auto swapchain_extent = render_backend->get_swapchain_extent();

    auto swapchain_color = render_graph->create_texture({
        .width = swapchain_extent.width,
        .height = swapchain_extent.height,
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
    
    render_backend->allocate_descriptor_sets_for_layout(
        light_layout,
        ResourceUsageType::Frame
    );
    
    
    render_backend->allocate_descriptor_sets_for_layout(
        tonemap_layout,
        ResourceUsageType::Frame
    );
    
    light_buffer = render_backend->create_uniform_buffer(
        sizeof(LightUBO),
        ResourceUsageType::Frame
    );
    
    render_backend->bind_buffer_to_descriptor(
        light_layout,
        BINDING_LIGHT_UBO,
        light_buffer
    );
    auto sampler = render_backend->create_sampler(RBSamplerDesc{
        .linear=true,
        .clamp_to_edge=true
    });
        
    
    render_graph->add_pass({
        .name = "GeometryForward",
        .writes = { 
            { hdr_color, RBImageUsage::ColorAttachment }, 
            { depth_texture, RBImageUsage::DepthStencilAttachment } 
        },
        .execute = [=](RenderGraphContext& ctx)
        {
            auto& extractor = engine->scene_view;
            auto cmd = ctx.cmd;
            
            // ---------- Camera ----------
            auto& camera_processor = extractor->get_processor<SceneViewProcessor_Camera>();

            const RenderObject_Camera* active_camera = camera_processor.get_active_camera();
            
            auto [width, height] = ctx.backend.get_viewport_extent();

            CameraUBO camera_ubo;
            camera_ubo.view_proj =
                active_camera->get_projection(float(width) / float(height)) *
                active_camera->view;
            camera_ubo.camera_pos = active_camera->position;

            ctx.backend.update_uniform_buffer(camera_buffer, camera_ubo);

            // ---------- Lights ----------
            auto& point_light_processor = extractor->get_processor<SceneViewProcessor_Light>();
            const auto [lights, light_num] = point_light_processor.query_nearest_lights_limited<8>(active_camera->position);
            
            LightUBO light_ubo{};
            light_ubo.light_count = light_num;
            for (int i = 0; i < light_num; ++i)
            {
                light_ubo.lights[i].position = glm::vec4(lights[i].position, 1.f);
                light_ubo.lights[i].color    = glm::vec4(lights[i].color);
            }

            ctx.backend.update_uniform_buffer(light_buffer, light_ubo);

            // ---------- Pipeline ----------
            ctx.backend.bind_pipeline(cmd, geometry_pipeline);

            ctx.backend.bind_descriptor_set(
                cmd, SET_CAMERA,
                ctx.backend.get_descriptor_set(camera_layout, ResourceUsageType::Frame),
                geometry_pipeline->get_pipeline_handle()
            );

            ctx.backend.bind_descriptor_set(
                cmd, SET_LIGHT,
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
                    cmd, SET_MATERIAL,
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
    
    render_graph->add_pass({
        .name = "ToneMapping",
        .reads = {
            { hdr_color, RBImageUsage::SampledFragment }
        },
        .writes = {
            { swapchain_color, RBImageUsage::ColorAttachment }
        },
        .execute = [=](RenderGraphContext& ctx)
        {
            render_backend->bind_image_to_descriptor(
                tonemap_layout,
                BINDING_HDR_COLOR,
                render_graph->get_image(hdr_color),
                sampler
            );
            ctx.backend.bind_pipeline(ctx.cmd, tonemap_pipeline);
            ctx.backend.bind_descriptor_set(
                ctx.cmd, SET_TONEMAPPING,
                ctx.backend.get_descriptor_set(tonemap_layout, ResourceUsageType::Frame),
                tonemap_pipeline->get_pipeline_handle()
            );
            ctx.backend.draw_fullscreen(ctx.cmd);
        }
    });

    

    render_graph->compile();
}

void GameRenderer::execute()
{
    auto& backend = *render_backend;
    
    RBFrameHandle frame = backend.get_current_frame();

    backend.wait_for_frame(frame);

    backend.reset_frame_fence(frame);

    if (!backend.acquire_next_image(frame))
    {
        render_graph->rebuild_resources();
        return;
    }

    RBCommandList cmd = backend.begin_commands(frame);
    render_graph->execute(cmd, frame);
    backend.end_commands(cmd);

    backend.submit_frame(frame, cmd);

    backend.advance_frame();

}

void GameRenderer::update_material_descriptor(const RenderMaterial& rm, const MaterialKey& key)
{
    render_backend->update_uniform_buffer(
        rm.material_ubo,
        MaterialUBO{
            .base_color_mult = key.base_color_mult,
            .emissive_mult = key.emissive_mult,
            .occlusion_mult = key.occlusion_mult,
            .roughness_mult = key.roughness_mult,
            .metallic_mult = key.metallic_mult,
        }
    );

    render_backend->update_sampled_image(
        rm.layout,
        BINDING_BASE_COLOR, // base_color
        get_texture(key.base_color),
        ResourceUsageType::Persistent
    );

    render_backend->update_sampled_image(
        rm.layout,
        BINDING_EMISSIVE, // emissive
        get_texture(key.emissive),
        ResourceUsageType::Persistent
    );

    render_backend->update_sampled_image(
        rm.layout,
        BINDING_NORMAL_MAP, // normal
        get_texture(key.normal),
        ResourceUsageType::Persistent
        );

    render_backend->update_sampled_image(
        rm.layout,
        BINDING_ORM, // occlusion_roughness_metallic
        get_texture(key.occlusion_roughness_metallic),
        ResourceUsageType::Persistent
    );
}

RBDescriptorSet GameRenderer::allocate_material_descriptor()
{
    auto result = render_backend->allocate_descriptor_sets_for_layout(
        material_layout,
        ResourceUsageType::Persistent
    );
    
    ensure(result.has_value());
    
    return *result;
}

RBBufferHandle GameRenderer::create_material_ubo()
{
    return render_backend->create_uniform_buffer(
        sizeof(MaterialUBO),
        ResourceUsageType::Persistent
    );
}

RBDescriptorSetLayout GameRenderer::get_material_layout() const
{
    return material_layout;
}

void GameRenderer::bind_material_ubo(const RenderMaterial& rm)
{
    render_backend->bind_buffer_to_descriptor(
        rm.layout,
        BINDING_MATERIAL_UBO,      // binding = 0
        rm.material_ubo
    );
}
