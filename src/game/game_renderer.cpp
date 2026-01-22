module game:renderer;

import render;
import vk;
import glm;
import rhmath;
import <vector>;
import profile;
import name;
import <unordered_map>;
import rhcomponents;

#include "render_layout.h"
#include "common/assertion_macros.h"
#include "profiling/profile.h"

void GameRenderer::set_engine(const std::shared_ptr<Engine>& in_engine)
{
    engine = in_engine;
}

namespace Names
{
    static Name pass_geometry_base = "GeometryBase";
    static Name pass_geometry_translucent = "GeometryTranslucent";
    
    static Name blend_mode = "blend_mode";
    static Name blend_mode_opaque = "opaque";
    static Name blend_mode_masked = "masked";
    static Name blend_mode_trasnlucent = "translucent";
}

void GameRenderer::init(RBWindowHandle in_window)
{
    Renderer::init(in_window);
    
    render_backend = RenderBackend::create<VkRenderBackend>(in_window);
    render_graph = std::make_shared<RenderGraph>(render_backend);

    samplers["default"] = render_backend->create_sampler(samplers::default_surface());
    samplers["surface"] = render_backend->create_sampler(samplers::default_surface());
    
    camera_resource = render_graph->create_resource({
        .name = "camera",
        .stages = ShaderStage::all,
        .usage_type = ResourceUsageType::frame,
        .variables = {
            { "camera", SET_CAMERA, BINDING_UBO_CAMERA, sizeof(CameraUBO) }
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
    
    
    auto& tonemap_model = models.find("tonemap")->second;
    
    RGTextureDesc hdr_color_desc{
        .name = "hdr_color",
        .width  = 0,
        .height = 0,
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
        .external = false
    };

    auto hdr_color = render_graph->create_texture(hdr_color_desc);
    
    VertexLayout vertex_layout = {
        VertexLayout {
            .layouts = {
                VertexLayoutData{
                    .binding_index = 0,
                    .stride = sizeof(Vertex),
                    .attributes = {
                          { "in_position",LOCATION_ATTR_POSITION, offsetof(Vertex, position) },
                          { "in_normal",  LOCATION_ATTR_NORMAL,   offsetof(Vertex, normal) },
                          { "in_uv",      LOCATION_ATTR_UV,       offsetof(Vertex, tex_coord) },
                          { "in_tangent", LOCATION_ATTR_TANGENT,  offsetof(Vertex, tangent) }
                    }
                }
            }
        }
    };
    
    geom_pipeline_layout = {
        .vertex_layout = vertex_layout,
        .resources = {camera_resource, light_resource }, 
        .push_constants = {{
            .stages = ShaderStage::vertex,
            .offset = 0,
            .size = sizeof(glm::mat4),
        }}
    };
    
    PipelineLayoutDesc tonemap_pipeline_layout = {
        .vertex_layout = {},
        .resources = {  }, 
        .push_constants = {}
    };
    
    
    PipelineFamily tonemap_pipeline_family("ToneMapping", tonemap_model, render_backend, shared_from_this());


    PipelineObject* tonemap_pipeline = render_graph->request_pipeline(
        tonemap_pipeline_family, {}, tonemap_pipeline_layout);
    
    auto swapchain_extent = render_backend->get_swapchain_extent();

    auto swapchain_color = render_graph->create_texture({
        .name = "swapchain",
        .width = swapchain_extent.width,
        .height = swapchain_extent.height,
        .format   = render_backend->get_swapchain_format(),
        .usage    = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Present,
        .external = true
    });
    
    auto depth_texture = render_graph->create_texture({
        .name = "depth",
        .format = TextureFormat::Depth24Stencil8,
        .usage  = RenderTextureUsage::DepthStencil
    });
    
    
    render_graph->add_pass({
        .name = Names::pass_geometry_base,
        .writes = { 
            // { hdr_color, RBImageUsage::ColorAttachment }, 
            { depth_texture, RBImageUsage::DepthStencilAttachment, RBLoadOp::Clear },
            { hdr_color, RBImageUsage::ColorAttachment, RBLoadOp::Clear }
        },
        .execute = [=](RenderGraphContext& ctx)
        {
            PROFILE("GeometryBase");
            
            draw_scene(ctx);
        }
    });
    
    render_graph->add_pass({
        .name = Names::pass_geometry_translucent,
        .reads = {
        },
        .writes = { 
            { depth_texture, RBImageUsage::DepthStencilReadOnly, RBLoadOp::Load },
            { hdr_color, RBImageUsage::ColorAttachment }, 
        },
        .execute = [=](RenderGraphContext& ctx)
        {
            PROFILE("Translucent");
            
            draw_scene(ctx);
        }
    });
    
    auto tonemap_material = std::make_shared<Material>();
    tonemap_material->model = "Tonemap";
    
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
            
            ctx.backend.bind_pipeline(ctx.cmd, tonemap_pipeline);
            
            std::shared_ptr<MaterialInstance> material_instance =
                get_or_create_material_instance(tonemap_material, ctx.pass_name);

            auto* tonemap_instance =
                material_instance->get_or_create_resource_instance(
                    tonemap_pipeline,
                    ctx.frame
                );
            
            tonemap_instance->update_image(
                tonemap_pipeline,
                "u_hdr_color",
                render_graph->get_image(hdr_color),
                ctx.frame
            );
            
            tonemap_instance->bind(
                tonemap_pipeline,
                ctx.cmd,
                ctx.frame
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

void GameRenderer::draw_scene(RenderGraphContext& ctx)
{    

    auto& scene_view = engine->scene_view;
    auto cmd = ctx.cmd;
    auto frame = ctx.frame;

    Name pass_name = ctx.pass_name;
    // ============================================================
    // 1. Pass-level resources (camera, light)
    // ============================================================

    PipelineObject* current_pipeline = nullptr;

    auto bind_pass_resources = [&](PipelineObject* pipeline)
    {
        if (pipeline == current_pipeline)
            return;

        current_pipeline = pipeline;

        // ---------- Camera ----------
        auto& camera_processor =
            scene_view->get_processor<SceneViewProcessor_Camera>();

        const RenderObject_Camera* cam_ro =
            camera_processor.get_active_camera();

        auto [width, height] = ctx.backend.get_viewport_extent();

        CameraUBO camera_ubo;
        camera_ubo.view_proj =
            cam_ro->get_projection(float(width) / float(height)) *
            cam_ro->view;
        camera_ubo.camera_pos = cam_ro->position;

        auto cam = camera_resource->query_single(pipeline);
        cam->update_uniform_buffer(pipeline, "camera", camera_ubo, frame);
        cam->bind(pipeline, cmd, frame);

        // ---------- Lights ----------
        auto& light_processor =
            scene_view->get_processor<SceneViewProcessor_Light>();

        const auto [lights, light_num] =
            light_processor.query_nearest_lights_limited<8>(cam_ro->position);

        LightUBO light_ubo{};
        light_ubo.light_count = light_num;
        for (int i = 0; i < light_num; ++i)
        {
            light_ubo.lights[i].position = glm::vec4(lights[i].position, 1.f);
            light_ubo.lights[i].color    = glm::vec4(lights[i].color);
        }

        auto light = light_resource->query_single(pipeline);
        light->update_uniform_buffer(pipeline, "light_ubo", light_ubo, frame);
        light->bind(pipeline, cmd, frame);
    };

    // ============================================================
    // 2. Collect draw items
    // ============================================================

    struct DrawItem
    {
        MeshPrimHandle mesh;
        glm::mat4 world;
        std::shared_ptr<Material> material;
    };

    using DrawBatch = std::vector<DrawItem>;
    std::unordered_map<ShaderKey, DrawBatch> batches;

    auto& meshes_processor =
        scene_view->get_processor<SceneViewProcessor_Mesh>();

    for (const auto& ro : meshes_processor.meshes)
    {
        for (uint32_t geom_index = 0;
             geom_index < ro.mesh.get().mesh_geometry.size();
             ++geom_index)
        {
            const auto& geom = ro.mesh.get().mesh_geometry[geom_index];

            for (uint32_t prim_index = 0;
                 prim_index < geom.primitives.size();
                 ++prim_index)
            {
                const auto& prim = geom.primitives[prim_index];

                uint32_t mat_index = prim.material_index.value_or(0);
                std::shared_ptr<Material> material = ro.mats[mat_index];
                
                Name blend_mode = material->get_enum_parameter(Names::blend_mode);
                
                const bool should_draw = 
                    (pass_name == Names::pass_geometry_base && blend_mode == Names::blend_mode_opaque) ||
                    (pass_name == Names::pass_geometry_translucent && blend_mode == Names::blend_mode_trasnlucent);    
                
                if (!should_draw)
                    continue;
                
                auto model = models.find(material->model)->second;

                // --------- Pipeline key ---------
                auto pipeline_family =
                    get_or_create_material_pipeline_family(
                        pass_name, model);

                ShaderKey key =
                    pipeline_family->make_shader_key(material, pass_name);

                batches[key].push_back({
                    .mesh     = MeshPrimHandle{ro.mesh, geom_index, prim_index},
                    .world    = ro.world,
                    .material = material
                });
            }
        }
    }

    // ============================================================
    // 3. Draw batches
    // ============================================================

    for (auto& [key, batch] : batches)
    {
        std::shared_ptr<Material> first_material = batch[0].material;
        
        auto model = models.find(first_material->model)->second;

        auto pipeline_family =
            get_or_create_material_pipeline_family(
                pass_name, model);

        PipelineObject* pipeline =
            pipeline_family->request_pipeline(key, geom_pipeline_layout);

        ctx.backend.bind_pipeline(cmd, pipeline);
        bind_pass_resources(pipeline);

        for (auto& item : batch)
        {
            // ---------- Material ----------
            std::shared_ptr<MaterialInstance> material_instance =
                get_or_create_material_instance(item.material, pass_name);

            material_instance->bind(pipeline, cmd, frame);

            // ---------- Mesh ----------
            ctx.backend.get_or_create_mesh_buffers(item.mesh);
            ctx.backend.bind_mesh(cmd, item.mesh, frame);

            // ---------- Push constants ----------
            ctx.backend.push_constants(cmd, item.world, pipeline);

            ctx.backend.draw_indexed(
                cmd,
                item.mesh.get().indices.size()
            );
        }
    }
}

std::shared_ptr<MaterialInstance> GameRenderer::get_or_create_material_instance(
    std::shared_ptr<Material> material, Name pass_name)
{
    auto it = material_instances.find({material, pass_name});
    if (it != material_instances.end())
        return it->second;

    auto ptr = std::static_pointer_cast<Renderer>(shared_from_this());
    
    auto instance = std::make_shared<MaterialInstance>(material, shared_from_this(), pass_name);

    material_instances.emplace(std::pair{material, pass_name}, instance);
    return instance;
}
