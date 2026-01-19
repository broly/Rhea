module game:renderer;

import render;
import vk;
import glm;
import rhmath;
import <vector>;
import profile;

#include "render_layout.h"
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
            { "camera", SET_CAMERA, BINDING_UBO_CAMERA, sizeof(CameraUBO) }
        }
    });
    
    
    model_resource = render_graph->create_resource({
        .name = "model_ubo",
        .stages = ShaderStage::all,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "model_ubo", SET_MODEL, BINDING_UBO_MODEL, sizeof(ModelUBO_Temp) }
        }
    });
    
    
    
    light_resource = render_graph->create_resource({
        .name = "light",
        .stages = ShaderStage::fragment,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "light_ubo", SET_LIGHT, BINDING_UBO_LIGHT, sizeof(LightUBO) }
        }
    });
    
    
    // TODO: hardcoded general material case
    material_resource = render_graph->create_resource({
        .name = "material",
        .stages = ShaderStage::fragment,
        .usage_type = ResourceUsageType::persistent,
        .sampler = surface_sampler,
        .variables = {
            { "material",  SET_MATERIAL, BINDING_UBO_MATERIAL, sizeof(MaterialUBO) },
            { "u_base_color", SET_MATERIAL, BINDING_SAMPLER_ALBEDO },
            { "u_emissive", SET_MATERIAL, BINDING_SAMPLER_EMISSIVE },
            { "u_normal_map", SET_MATERIAL, BINDING_SAMPLER_NORMAL },
            { "u_orm", SET_MATERIAL, BINDING_SAMPLER_ORM },
        },
    });
    
    auto tonemap_resource = render_graph->create_resource({
        .name = "tonemap",
        .stages = ShaderStage::fragment,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "u_hdr_color", SET_TONEMAP, BINDING_UBO_TONEMAP },
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
        .features = {
            ShaderFeatureEnum("BLEND_MODE", {"BLEND_MODE_OPAQUE", "BLEND_MODE_MASKED", "BLEND_MODE_TRANSPARENT"}),
            ShaderFeatureFlag("USE_DEBUG"),
            ShaderFeatureFlag("USE_NORMAL"),
        },
        .stages = {
            {
                .stage = ShaderStage::vertex,
                .shader = "geometry.vert",
                .vertex_layouts = {
                    VertexLayoutData {
                            .binding_index = 0,
                            .stride = sizeof(Vertex),
                            .attributes = {
                                { "in_position", LOCATION_ATTR_POSITION, offsetof(Vertex, position) },
                                { "in_normal", LOCATION_ATTR_NORMAL, offsetof(Vertex, normal) },
                                { "in_uv", LOCATION_ATTR_UV, offsetof(Vertex, tex_coord) },
                                { "in_tangent", LOCATION_ATTR_TANGENT, offsetof(Vertex, tangent) }
                            }
                        }
                    }
            },
            {
                .stage = ShaderStage::fragment,
                .shader = "geometry.frag"
            }
        },
        
        .layout = {
            .resources = {camera_resource, material_resource, light_resource, model_resource }, 
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
    
    PipelineFamily geom_pipeline_family(geom_pipeline_desc, render_backend);
    
    
    GraphicsPipelineDesc tonemap_pipeline_desc{
        .features = {},
        .stages = {
            {
                .stage = ShaderStage::vertex,
                .shader = "fullscreen.vert",
            },
            {
                .stage = ShaderStage::fragment,
                .shader = "tonemap.frag",
            }
        },
        .layout = {
            .resources = { tonemap_resource },
        },
        .rt_compat = {
            .color_attachment_count = 1,
            .has_depth = false
        }
    };
    
    ShaderKey geometry_opaque_shader_key = geom_pipeline_family.make_shader_key({
        {"BLEND_MODE", "BLEND_MODE_TRANSPARENT"},
        {"USE_DEBUG", true},
        {"USE_NORMAL", true},
    });

    auto geometry_opaque = render_graph->request_pipeline(geom_pipeline_family, geometry_opaque_shader_key);
    geom_pipeline = geometry_opaque;
    
    
    PipelineFamily tonemap_pipeline_family(tonemap_pipeline_desc, render_backend);
    ShaderKey tonemap_shader_key = tonemap_pipeline_family.make_shader_key({});
    auto tonemap_pipeline = render_graph->request_pipeline(tonemap_pipeline_family, tonemap_shader_key);
    
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
        .name = "GeometryBase",
        .pipelines = {geometry_opaque},
        .writes = { 
            { hdr_color, RBImageUsage::ColorAttachment }, 
            { depth_texture, RBImageUsage::DepthStencilAttachment } 
        },
        .execute = [=](RenderGraphContext& ctx)
        {
            PROFILE("GeometryBase");

            auto pipeline = ctx.pipelines[0];
            
            ctx.backend.bind_pipeline(ctx.cmd, pipeline);
            
            draw_scene(ctx, pipeline);
        }
    });
    
    render_graph->add_pass({
        .name = "ToneMapping",
        .pipelines = {tonemap_pipeline},
        .reads = {
            { hdr_color, RBImageUsage::SampledFragment }
        },
        .writes = {
            { swapchain_color, RBImageUsage::ColorAttachment }
        },
        .execute = [=](RenderGraphContext& ctx)
        {
            auto pipeline = ctx.pipelines[0];
            
            ctx.backend.bind_pipeline(ctx.cmd, pipeline);
            auto tonemap = tonemap_resource->query_single();
            tonemap->update_image(pipeline, "u_hdr_color", render_graph->get_image(hdr_color), ctx.frame);
            tonemap->bind(pipeline, ctx.cmd, ctx.frame);
            
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
    return material_resource;
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

void GameRenderer::draw_scene(RenderGraphContext& ctx, PipelineObject* pipeline) const
{    
    auto& scene_view = engine->scene_view;
    auto cmd = ctx.cmd;
            
    // ---------- Camera ----------
    auto& camera_processor = scene_view->get_processor<SceneViewProcessor_Camera>();

    const RenderObject_Camera* active_camera = camera_processor.get_active_camera();
            
    auto [width, height] = ctx.backend.get_viewport_extent();

    CameraUBO camera_ubo;
    camera_ubo.view_proj =
        active_camera->get_projection(float(width) / float(height)) *
        active_camera->view;
    camera_ubo.camera_pos = active_camera->position;
            
    auto cam = camera_resource->query_single();
    cam->update_uniform_buffer(pipeline, "camera", camera_ubo, ctx.frame);
    cam->bind(pipeline, cmd, ctx.frame);

            
    // ---------- Lights ----------
    auto& point_light_processor = scene_view->get_processor<SceneViewProcessor_Light>();
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
        light->update_uniform_buffer(pipeline, "light_ubo", light_ubo, ctx.frame);
        light->bind(pipeline, cmd, ctx.frame);
    }

    
    // ---------- Draw ----------
    auto& meshes_processor = scene_view->get_processor<SceneViewProcessor_Mesh>();
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
                    
                material_resource_instance->bind(pipeline, cmd, ctx.frame);
        
                ctx.backend.get_or_create_mesh_buffers(mesh_prim);
                
                ctx.backend.bind_mesh(cmd, mesh_prim, ctx.frame);
                
                auto model = model_resource->query_single();
                ModelUBO_Temp model_ubo{ro.world};
                model->update_uniform_buffer(pipeline, "model_ubo", model_ubo, ctx.frame);
                model->bind(pipeline, cmd, ctx.frame);
                ctx.backend.push_constants(
                    cmd, ro.world,
                    pipeline
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


