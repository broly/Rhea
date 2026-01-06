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
    
    auto camera_resource = render_graph->create_resource({
        .name = "camera",
        .stages = ShaderStage::all,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "camera", sizeof(CameraUBO) }
        }
    });
    
    auto light_resource = render_graph->create_resource({
        .name = "light",
        .stages = ShaderStage::fragment,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "light_ubo", sizeof(LightUBO) }
        }
    });
    
    auto material_resource = render_graph->create_resource({
        .name = "material",
        .stages = ShaderStage::fragment,
        .usage_type = ResourceUsageType::persistent,
        .variables = {
            { "material", sizeof(MaterialUBO) },
            { "u_base_color" },
            { "u_emissive" },
            { "u_normal_map" },
            { "u_orm" },
        }
    });
    
    _mat_res = material_resource;
    
    auto tonemap_resource = render_graph->create_resource({
        .name = "tonemap",
        .stages = ShaderStage::fragment,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "u_hdr_color" },
        },
    });
    
    RGTextureDesc hdr_color_desc{
        .width  = 0,
        .height = 0,
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
        .external = false
    };

    auto hdr_color = render_graph->create_texture(hdr_color_desc);
    
    
    GraphicsPipelineDesc geom_pipeline_desc{
        .stages = {
            {
                .stage = ShaderStage::vertex,
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
                .stage = ShaderStage::fragment,
                .shader = "shaders/geometry.frag.spv"
            }
        },
        .layout = {
            .sets = {}, 
            .push_constants = {{
                .stages = ShaderStage::vertex,
                .offset = 0,
                .size = sizeof(glm::mat4),
            }}
        },
        .rt_compat = {
            .color_attachment_count = 1,
            .has_depth = true
        }
    };
    
    
    GraphicsPipelineDesc tonemap_pipeline_desc{
        .stages = {
            {
                .stage = ShaderStage::vertex,
                .shader = "shaders/fullscreen.vert.spv",
            },
            {
                .stage = ShaderStage::fragment,
                .shader = "shaders/tonemap.frag.spv",
            }
        },
        .layout = {
            .sets = {  },
        },
        .rt_compat = {
            .color_attachment_count = 1,
            .has_depth = false
        }
    };
    auto tonemap_pipeline = render_graph->create_pipeline(tonemap_pipeline_desc);
    auto geometry_pipeline = render_graph->create_pipeline(geom_pipeline_desc);
    
    geom_pipeline = geometry_pipeline;
    
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
    
    auto sampler = render_backend->create_sampler(RBSamplerDesc{
        .linear=true,
        .clamp_to_edge=true
    });
        
    
    render_graph->add_pass({
        .name = "GeometryForward",
        .pipeline = geometry_pipeline,  // added this
        .writes = { 
        { hdr_color, RBImageUsage::ColorAttachment }, 
        { depth_texture, RBImageUsage::DepthStencilAttachment } 
        },
        .resources = { camera_resource, material_resource, light_resource },    
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
            
            auto cam = camera_resource->query_single();
            cam->update_uniform_buffer(ctx.pipeline, "camera", camera_ubo);
            cam->bind(ctx.pipeline, cmd);

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

            auto light = light_resource->query_single();
            light->update_uniform_buffer(ctx.pipeline, "light_ubo", light_ubo);
            light->bind(ctx.pipeline, cmd);


            // ---------- Draw ----------
            
            auto& meshes_processor = extractor->get_processor<SceneViewProcessor_Mesh>();
            for (const auto& ro : meshes_processor.meshes)
            {
                auto material_resource_instance = meshes_processor.get_or_create_material_resource(
                    material_resource, ro.material_key);
                
                material_resource_instance->bind(ctx.pipeline, cmd);
                
                ctx.backend.get_or_create_mesh_buffers(ro.mesh);

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
        .pipeline = tonemap_pipeline,
        .reads = {
            { hdr_color, RBImageUsage::SampledFragment }
        },
        .writes = {
            { swapchain_color, RBImageUsage::ColorAttachment }
        },
        .resources = { tonemap_resource },
        .execute = [=](RenderGraphContext& ctx)
        {
            auto tonemap = tonemap_resource->query_single();
            tonemap->update_image(ctx.pipeline, "u_hdr_color", render_graph->get_image(hdr_color));
            tonemap->bind(ctx.pipeline, ctx.cmd);
            
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

RenderResource* GameRenderer::get_material_resource()
{
    return _mat_res;
}

void GameRenderer::update_material_resource(RenderResourceInstance* material_resource_instance, MaterialKey material_key)
{
    
    material_resource_instance->update_uniform_buffer(geom_pipeline, "material", 
        MaterialUBO{
            .base_color_mult = material_key.base_color_mult,
            .emissive_mult = material_key.emissive_mult,
            .occlusion_mult = material_key.occlusion_mult,
            .roughness_mult = material_key.roughness_mult,
            .metallic_mult = material_key.metallic_mult,
        });
                
    material_resource_instance->update_image(geom_pipeline, "u_base_color", 
        get_texture(material_key.base_color));
    material_resource_instance->update_image(geom_pipeline, "u_emissive", 
        get_texture(material_key.emissive));
    material_resource_instance->update_image(geom_pipeline, "u_normal_map", 
        get_texture(material_key.normal));
    material_resource_instance->update_image(geom_pipeline, "u_orm", 
        get_texture(material_key.occlusion_roughness_metallic));                
                
}


