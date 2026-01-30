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
    
    static Name debug_shadow = "debug_shadow";
}

constexpr bool debug_shadow_pass = false;

void GameRenderer::init(RBWindowHandle in_window)
{
    set_flag(Names::debug_shadow, false);
    
    render_backend = RenderBackend::create<VkRenderBackend>(in_window);
    render_graph = std::make_shared<RenderGraph>(render_backend);
    
    Renderer::init(in_window);
    
    samplers["default"] = render_backend->create_sampler(samplers::default_surface());
    samplers["surface"] = render_backend->create_sampler(samplers::default_surface());
    samplers["shadow"] = render_backend->create_sampler(samplers::default_shadow());
    
    camera_resource = find_resource("camera");
    light_resource = find_resource("light");
    light_resource_shadow = find_resource("shadow_light");
    shadow_resource = find_resource("shadow");
    
    auto& tonemap_model = models.find("tonemap")->second;
    auto& shadow_debug_model = models.find("shadow_debug")->second;
    
    RGTextureDesc hdr_color_desc{
        .name = "hdr_color",
        .width  = 0,
        .height = 0,
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
        .external = false
    };

    auto hdr_color = render_graph->create_texture(hdr_color_desc);
    
    PipelineFamily tonemap_pipeline_family("ToneMapping", tonemap_model, render_backend, shared_from_this());
    
    PipelineObject* tonemap_pipeline = render_graph->request_pipeline(
        tonemap_pipeline_family, {});
    
    PipelineFamily shadow_debug_pipeline_family("ShadowDebug", shadow_debug_model, render_backend, shared_from_this());
    PipelineObject* shadow_debug_pipeline =
        shadow_debug_pipeline_family.request_pipeline({});
    auto shadow_debug_material = std::make_shared<Material>();
    shadow_debug_material->model = "shadow_debug";
    
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
    
    shadowmap_extent = {2048, 2048};
    
    shadow_map = render_graph->create_texture({
        .name = "shadow_map",
        .width = shadowmap_extent.width,
        .height = shadowmap_extent.height,
        .format = TextureFormat::Depth32F,
        .usage = RenderTextureUsage::DepthStencil | RenderTextureUsage::Sampled
    });
    
    render_graph->add_pass({
        .name = "ShadowMap",
        .writes = {
            { shadow_map, RBImageUsage::DepthStencilAttachment, RBLoadOp::Clear }
        },
        .execute = [=](RenderGraphContext& ctx)
        {
            draw_scene_shadow(ctx);
        }
    });
    
    render_graph->add_pass({
        .name = "ShadowDebug",
        .condition = [this] () { return render_flags[Names::debug_shadow]; },
        .reads = {
            { shadow_map, RBImageUsage::SampledFragment }
        },
        .writes = {
            { swapchain_color, RBImageUsage::ColorAttachment, RBLoadOp::Clear }
        },
        .execute = [=](RenderGraphContext& ctx)
        {
            ctx.backend.bind_pipeline(ctx.cmd, shadow_debug_pipeline);
            
            auto shadow_debug_material_instance = get_or_create_material_instance(shadow_debug_material, ctx.pass_name);
    
            RenderResourceInstance* shadow_debug_instance =
                shadow_debug_material_instance->get_or_create_resource_instance(
                    shadow_debug_pipeline,
                    ctx.frame
                );
    
            shadow_debug_instance->update_image(
                shadow_debug_pipeline,
                "u_shadow_depth",
                render_graph->get_image(shadow_map),
                ctx.frame
            );
    
            shadow_debug_instance->bind(
                shadow_debug_pipeline,
                ctx.cmd,
                ctx.frame
            );
    
            ctx.backend.draw_fullscreen(ctx.cmd);
        }
    });
    
    render_graph->add_pass({
        .name = Names::pass_geometry_base,
        .reads = {
            {  shadow_map, RBImageUsage::SampledFragment }
        },
        .writes = { 
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
            { depth_texture, RBImageUsage::DepthStencilAttachment, RBLoadOp::Load },
            { hdr_color, RBImageUsage::ColorAttachment, RBLoadOp::Load },   
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
        .condition = [this] () { return !is_debugging(); },
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
    
    if (render_graph_needs_rebuild)
    {
        render_graph->recompile();
        render_graph_needs_rebuild = false;
    }
    
    RBFrameHandle frame = backend.get_current_frame();

    backend.wait_for_frame(frame);

    backend.reset_frame_fence(frame);

    if (!backend.acquire_next_image(frame))
    {
        render_graph->rebuild_resources();
        return;
    }

    RBCommandList cmd = backend.begin_commands(frame);
    RenderGraphParameters params;
    params.mode = RenderGraphMode::Camera;
    render_graph->execute(cmd, frame, params);
    backend.end_commands(cmd);

    backend.submit_frame(frame, cmd);

    backend.advance_frame();

}

bool GameRenderer::is_debugging() const
{
    auto result = render_flags.find(Names::debug_shadow);
    return result != render_flags.end() && result->second;
}

glm::mat4 GameRenderer::build_dir_light_vp() const
{
    auto& scene_view = engine->scene_view;
    auto& light_processor = scene_view->get_processor<SceneViewProcessor_Light>();

    auto dir = light_processor.get_directional_light();
    if (!dir)
        return glm::mat4(1.0f);

    const AABB& aabb = scene_view->world_aabb;
    glm::vec3 center = aabb.center();

    glm::vec3 lightDir = glm::normalize(dir->direction);

    glm::vec3 up =
        fabs(glm::dot(lightDir, glm::vec3(0,1,0))) > 0.99f
            ? glm::vec3(0,0,1)
            : glm::vec3(0,1,0);

    float dist = aabb.scalar_radius();
    glm::vec3 lightPos = center - lightDir * dist;

    glm::mat4 lightView = glm::lookAt(lightPos, center, up);

    glm::vec3 corners[8];
    aabb.get_corners(corners);

    glm::vec3 minLS( FLT_MAX);
    glm::vec3 maxLS(-FLT_MAX);

    for (int i = 0; i < 8; ++i)
    {
        glm::vec3 p = glm::vec3(lightView * glm::vec4(corners[i], 1.0f));
        minLS = glm::min(minLS, p);
        maxLS = glm::max(maxLS, p);
    }

    
    glm::mat4 lightProj = glm::orthoZO(
        minLS.x, maxLS.x,
        minLS.y, maxLS.y,
        0.f, -minLS.z * 10
    );

    return lightProj * lightView;
}


void GameRenderer::draw_scene(RenderGraphContext& ctx)
{    

    auto& scene_view = engine->scene_view;
    auto cmd = ctx.cmd;
    auto frame = ctx.frame;

    Name pass_name = ctx.pass_name;

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
        camera_ubo.view_proj  = 
            cam_ro->get_projection(float(width) / float(height)) *
            cam_ro->view;
        camera_ubo.camera_pos = cam_ro->position;

        auto cam = camera_resource->query_single(pipeline);
        cam->update_uniform_buffer(pipeline, "camera_ubo", camera_ubo, frame);
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
        
        const auto dir_light = light_processor.get_directional_light();
        if (dir_light)
        {
            light_ubo.has_dir_light = true;
            light_ubo.dir_light.direction = glm::vec4(glm::normalize(dir_light->direction), 0.f);
            light_ubo.dir_light.color = glm::vec4(dir_light->color);

            light_ubo.dir_light.light_vp = build_dir_light_vp();
        }
        

        auto light = light_resource->query_single(pipeline);
        light->update_uniform_buffer(pipeline, "light_ubo", light_ubo, frame);
        light->bind(pipeline, cmd, frame);
        
        auto shadow = shadow_resource->query_single(pipeline);
        shadow->update_image(pipeline, "u_shadow_depth", 
                render_graph->get_image(shadow_map), frame);
        shadow->bind(pipeline, cmd, frame);
    };



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


    for (auto& [key, batch] : batches)
    {
        std::shared_ptr<Material> first_material = batch[0].material;
        
        auto model = models.find(first_material->model)->second;

        auto pipeline_family =
            get_or_create_material_pipeline_family(
                pass_name, model);

        PipelineObject* pipeline =
            pipeline_family->request_pipeline(key);

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
                item.mesh.get().indices.size(),
                RBDrawParams {
                    .update_viewport_extent = true,
                    .use_swapchain_extent = true
                }
            );
        }
    }
}

void GameRenderer::draw_scene_shadow(RenderGraphContext& ctx)
{
    PROFILE("ShadowPass");

    auto& scene_view = engine->scene_view;
    auto cmd = ctx.cmd;
    auto frame = ctx.frame;

    PipelineObject* current_pipeline = nullptr;

    auto bind_light = [&](PipelineObject* pipeline)
    {
        if (pipeline == current_pipeline)
            return;

        current_pipeline = pipeline;

        auto& light_processor =
            scene_view->get_processor<SceneViewProcessor_Light>();

        LightUBO light_ubo{};
        light_ubo.has_dir_light = 0;

        for (auto& l : light_processor.lights)
        {
            if (l.type == LightType::directional)
            {
                light_ubo.has_dir_light = 1;
                light_ubo.dir_light.direction = glm::vec4(glm::normalize(l.direction), 0.0f);
                light_ubo.dir_light.color     = l.color;

                auto& camera_processor = scene_view->get_processor<SceneViewProcessor_Camera>();
                const RenderObject_Camera* cam_ro = camera_processor.get_active_camera();

                light_ubo.dir_light.light_vp = build_dir_light_vp();

                break; 
            }

        }

        auto light = light_resource_shadow->query_single(pipeline);
        light->update_uniform_buffer(
            pipeline, "shadow_light_ubo", light_ubo, frame);
        light->bind(pipeline, cmd, frame);
    };

    // =======================
    // Collect draw items
    // =======================

    struct DrawItem
    {
        MeshPrimHandle mesh;
        glm::mat4 world;
    };

    std::vector<DrawItem> items;

    auto& mesh_processor =
        scene_view->get_processor<SceneViewProcessor_Mesh>();

    for (const auto& ro : mesh_processor.meshes)
    {
        for (uint32_t geom = 0; geom < ro.mesh.get().mesh_geometry.size(); ++geom)
        {
            for (uint32_t prim = 0;
                 prim < ro.mesh.get().mesh_geometry[geom].primitives.size();
                 ++prim)
            {
                uint32_t mat_index =
                    ro.mesh.get().mesh_geometry[geom]
                        .primitives[prim].material_index.value_or(0);

                auto material = ro.mats[mat_index];
                Name blend_mode =
                    material->get_enum_parameter(Names::blend_mode);

                if (blend_mode != Names::blend_mode_opaque)
                    continue;

                items.push_back({
                    .mesh = MeshPrimHandle{ro.mesh, geom, prim},
                    .world = ro.world
                });
            }
        }
    }

    // =======================
    // Draw
    // =======================

    for (auto& item : items)
    {
        auto pipeline_family =
            get_or_create_material_pipeline_family(
                "ShadowMap",
                models.find("Shadow")->second
            );

        PipelineObject* pipeline =
            pipeline_family->request_pipeline({});

        ctx.backend.bind_pipeline(cmd, pipeline);
        bind_light(pipeline);

        ctx.backend.get_or_create_mesh_buffers(item.mesh);
        ctx.backend.bind_mesh(cmd, item.mesh, frame);

        ctx.backend.push_constants(cmd, item.world, pipeline);

        ctx.backend.draw_indexed(
            cmd,
            item.mesh.get().indices.size(),
             RBDrawParams{
                 .update_viewport_extent = true,
                 .use_swapchain_extent = false,
                 .extent = shadowmap_extent
             }
        );
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

