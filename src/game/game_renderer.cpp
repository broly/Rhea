module game:renderer;

import render;
import vk;
import glm;
import rhmath;
import <vector>;
import profile;

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
#include "profiling/profile.h"

void GameRenderer::set_engine(const std::shared_ptr<Engine>& in_engine)
{
    engine = in_engine;
}

void GameRenderer::init(RBWindowHandle in_window)
{
    Renderer::init(in_window);
    
    render_backend = RenderBackend::create<VkRenderBackend>(in_window);
    render_graph = std::make_shared<RenderGraph>(render_backend);

    auto surface_sampler = render_backend->create_sampler(samplers::default_surface());
    auto default_sampler = render_backend->create_sampler(samplers::default_surface());
    
    camera_resource = render_graph->create_resource({
        .name = "camera",
        .stages = ShaderStage::all,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "camera", sizeof(CameraUBO) }
        }
    });
    
    
    model_resource = render_graph->create_resource({
        .name = "model_ubo",
        .stages = ShaderStage::all,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "model_ubo", sizeof(ModelUBO_Temp) }
        }
    });
    
    
    
    light_resource = render_graph->create_resource({
        .name = "light",
        .stages = ShaderStage::fragment,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "light_ubo", sizeof(LightUBO) }
        }
    });
    
    material_resource = render_graph->create_resource({
        .name = "material",
        .stages = ShaderStage::fragment,
        .usage_type = ResourceUsageType::persistent,
        .sampler = surface_sampler,
        .variables = {
            { "material", sizeof(MaterialUBO) },
            { "u_base_color" },
            { "u_emissive" },
            { "u_normal_map" },
            { "u_orm" },
        },
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
    
    
    render_graph->add_pass({
        .name = "GeometryForward",
        .pipeline = geometry_pipeline,  // added this
        .writes = { 
            { hdr_color, RBImageUsage::ColorAttachment }, 
            { depth_texture, RBImageUsage::DepthStencilAttachment } 
        },
        .resources = { 
            camera_resource, 
            material_resource, 
            light_resource, 
            model_resource 
        },    
        .execute = [=](RenderGraphContext& ctx)
        {
            PROFILE("GeometryForward");

            draw_scene(ctx);
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
            tonemap->update_image(ctx.pipeline, "u_hdr_color", render_graph->get_image(hdr_color), ctx.frame);
            tonemap->bind(ctx.pipeline, ctx.cmd, ctx.frame);
            
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

void GameRenderer::update_material_resource(RenderResourceInstance* material_resource_instance, MaterialKey material_key, RBFrameHandle frame)
{
    PROFILE("GeometryForward:update_material_resource");
    material_resource_instance->update_uniform_buffer(geom_pipeline, "material", 
                                                      MaterialUBO{
                                                          .base_color_mult = material_key.base_color_mult,
                                                          .emissive_mult = material_key.emissive_mult,
                                                          .occlusion_mult = material_key.occlusion_mult,
                                                          .roughness_mult = material_key.roughness_mult,
                                                          .metallic_mult = material_key.metallic_mult,
                                                      }, frame);
                
    material_resource_instance->update_image(geom_pipeline, "u_base_color", 
                                             get_texture(material_key.base_color), frame);
    material_resource_instance->update_image(geom_pipeline, "u_emissive", 
                                             get_texture(material_key.emissive), frame);
    material_resource_instance->update_image(geom_pipeline, "u_normal_map", 
                                             get_texture(material_key.normal), frame);
    material_resource_instance->update_image(geom_pipeline, "u_orm", 
                                             get_texture(material_key.occlusion_roughness_metallic), frame);                
                
}

void GameRenderer::draw_scene(RenderGraphContext& ctx) const
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
    cam->update_uniform_buffer(ctx.pipeline, "camera", camera_ubo, ctx.frame);
    cam->bind(ctx.pipeline, cmd, ctx.frame);

            
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
    {                
        auto light = light_resource->query_single();
        light->update_uniform_buffer(ctx.pipeline, "light_ubo", light_ubo, ctx.frame);
        light->bind(ctx.pipeline, cmd, ctx.frame);
    }

    
    // ---------- Draw ----------
    auto& meshes_processor = extractor->get_processor<SceneViewProcessor_Mesh>();
    for (const auto& ro : meshes_processor.meshes)
    {
        checkf(ro.material_keys.size() == ro.material_instances.size(), "size differs. invalid behaviour");
        
        for (uint32_t geom_index = 0; auto& geom : ro.mesh.get().mesh_geometry)
        {
            for (uint32_t prim_index = 0; auto& prim : geom.primitives)
            {
                MeshPrimHandle mesh_prim{ro.mesh, geom_index, prim_index};

                uint32_t material_index = prim.material_index.value_or(0);
                auto material_key = ro.material_keys[material_index]; // 0 is temp crutch
                    
                auto material_resource_instance = meshes_processor.get_or_create_material_resource(
                    material_resource, material_key);
                    
                material_resource_instance->bind(ctx.pipeline, cmd, ctx.frame);
        
                ctx.backend.get_or_create_mesh_buffers(mesh_prim);
                
                ctx.backend.bind_mesh(cmd, mesh_prim, ctx.frame);
                
                auto model = model_resource->query_single();
                ModelUBO_Temp model_ubo{ro.world};
                model->update_uniform_buffer(ctx.pipeline, "model_ubo", model_ubo, ctx.frame);
                model->bind(ctx.pipeline, cmd, ctx.frame);
                ctx.backend.push_constants(
                    cmd, ro.world,
                    ctx.pipeline->get_pipeline_handle()
                );

                ctx.backend.draw_indexed(
                    cmd,
                    prim.indices.size()
                );
                prim_index++;
            }
            geom_index++;
        }
        
    }
}


