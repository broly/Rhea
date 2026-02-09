module game:generic_render_graph;


import render;
import vk;
import glm;
import rhmath;
import <vector>;
import profile;
import name;
import <unordered_map>;
import rhcomponents;
import globals;
import assets;
import <functional>;
import texture_format;
import :constants;
import :names;

#include "render_layout.h"
#include "common/assertion_macros.h"
#include "profiling/profile.h"




#include "common/assertion_macros.h"
#include "profiling/profile.h"


GenericRenderGraph::GenericRenderGraph()
{
    allow_shadow_debug = false;
    resolution = Constants::zero_extent;
    capture_dimension = TextureDimension::Tex2D;
    num_pass_instances = 1;
}

void GenericRenderGraph::init_resources(const std::map<Name, bool>& parameters)
{
    engine = RhGlobals::engine;
    
    camera_resource = renderer->find_resource("camera");
    light_resource = renderer->find_resource("light");
    light_resource_shadow = renderer->find_resource("shadow_light");
    shadow_resource = renderer->find_resource("shadow");
    reflection_resource = renderer->find_resource("reflection");
    
    
    
    auto& asset_manager = AssetManager::get();
    auto noise_tex = asset_manager.load_texture("textures/noise/noise_512.png");
    noise_texture = create_texture_from_asset(noise_tex, false);
    
    auto brdf_lut_tex = asset_manager.load_texture("textures/brdf_lut.png");
    brdf_lut = create_texture_from_asset(brdf_lut_tex, false);
    
    auto tonemap_model = renderer->find_model("tonemap");
    auto shadow_debug_model = renderer->find_model("shadow_debug");
    
    
    swapchain_extent = backend->get_swapchain_extent();
    
    
    RGTextureDesc hdr_color_desc{
        .name = "hdr_color",
        .extent = resolution,
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
        .external = false,
        .dimension = capture_dimension
    };
    
    hdr_color = create_texture(hdr_color_desc);
    
    
    PipelineFamily tonemap_pipeline_family("ToneMapping", tonemap_model, backend, renderer);
    
    tonemap_pipeline = request_pipeline(
        tonemap_pipeline_family, {});
    
    PipelineFamily shadow_debug_pipeline_family("ShadowDebug", shadow_debug_model, backend, renderer);
    shadow_debug_pipeline =
        shadow_debug_pipeline_family.request_pipeline({});
    shadow_debug_material = std::make_shared<Material>();
    shadow_debug_material->model = "shadow_debug";
    
    
    depth_texture = create_texture({
        .name = "depth",
        .extent = resolution,
        .format = TextureFormat::Depth24Stencil8,
        .usage = RenderTextureUsage::DepthStencil | RenderTextureUsage::Sampled,
        .dimension = capture_dimension
    });
    
    
    shadow_map = create_texture({
        .name = "shadow_map",
        .extent = Constants::shadowmap_extent,
        .format = TextureFormat::Depth32F,
        .usage = RenderTextureUsage::DepthStencil | RenderTextureUsage::Sampled
    });
    


    tonemap_material = std::make_shared<Material>();
    tonemap_material->model = "Tonemap";
}

void GenericRenderGraph::build_passes(const std::map<Name, bool>& parameters)
{
    add_pass({
        .name = "ShadowMap",
        .writes = {
            { shadow_map, RBImageUsage::DepthStencilAttachment, RBLoadOp::Clear }
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            draw_scene_shadow(ctx);
        },
    });
    
    if (allow_shadow_debug)
    {
        add_pass({
           .name = "ShadowDebug",
           .condition = [this] () { return renderer->get_render_flag(Names::debug_shadow); },
           .reads = {
               { shadow_map, RBImageUsage::SampledFragment }
           },
           .writes = {
               { swapchain_color, RBImageUsage::ColorAttachment, RBLoadOp::Clear }
           },
           .execute = [this] (RenderGraphContext& ctx)
           {
                ctx.backend.bind_pipeline(ctx.cmd, shadow_debug_pipeline);
            
                auto shadow_debug_material_instance = 
                    renderer->query_material_instance(shadow_debug_material, ctx.pass_name);
                
                RenderResourceInstance* shadow_debug_instance =
                    shadow_debug_material_instance->get_or_create_resource_instance(
                        shadow_debug_pipeline,
                        ctx.frame
                    );
                
                    shadow_debug_instance->update_image(
                        shadow_debug_pipeline,
                        "u_shadow_depth",
                        get_image(shadow_map),
                        ctx.frame
                    );
                    
                shadow_debug_instance->bind(
                    shadow_debug_pipeline,
                    ctx.cmd,
                    ctx.frame
                );
                
                ctx.backend.draw_fullscreen(ctx.cmd);
           },
       });
    }
    
    add_pass({
        .name = Names::pass_geometry_base,
        .reads = {
            {  shadow_map, RBImageUsage::SampledFragment },
            { brdf_lut, RBImageUsage::SampledFragment, RBLoadOp::Load },
        },
        .writes = { 
            { depth_texture, RBImageUsage::DepthStencilAttachment, RBLoadOp::Clear },
            { hdr_color, RBImageUsage::ColorAttachment, RBLoadOp::Clear },
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            PROFILE("Geometry");
            
            draw_scene(ctx);
        },
        .num_layers = num_pass_instances
    });
    
    
    add_pass({
        .name = Names::pass_geometry_translucent,
        .reads = {
            { brdf_lut, RBImageUsage::SampledFragment, RBLoadOp::Load },    
        },
        .writes = { 
            { depth_texture, RBImageUsage::DepthStencilAttachment, RBLoadOp::Load },
            { hdr_color, RBImageUsage::ColorAttachment, RBLoadOp::Load },  
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            PROFILE("Translucent");
            
            draw_scene(ctx);
        },
        .num_layers = num_pass_instances
    });
    
    
    
    add_pass({
        .name = "Clouds",
        .reads = {
            { depth_texture, RBImageUsage::SampledFragment },
            { noise_texture, RBImageUsage::SampledFragment }
        },
        .writes = {
            { hdr_color, RBImageUsage::ColorAttachment, RBLoadOp::Load }
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            PROFILE("Clouds");
            
            draw_clouds(ctx, depth_texture, noise_texture);
        },
        .num_layers = num_pass_instances
        
    });
}

void GenericRenderGraph::prepare_resources(RenderGraphContext& ctx)
{
    RenderGraph::prepare_resources(ctx);
    
    prepared_batches.clear();
    prepared_pass_resources.clear();

    // -------- batching --------
    prepare_geometry_batches(ctx, Names::pass_geometry_base);
    prepare_geometry_batches(ctx, Names::pass_geometry_translucent);

    // -------- resources --------
    prepare_geometry_resources(ctx, Names::pass_geometry_base);
    prepare_geometry_resources(ctx, Names::pass_geometry_translucent);
    
    
    prepare_clouds_pass(ctx);
}

LightUBO GenericRenderGraph::build_light_ubo(glm::vec3 camera_position) const
{
    auto& scene_view = engine->scene_view;
    auto& light_processor =
        scene_view->get_processor<SceneViewProcessor_Light>();

    LightUBO light_ubo{};
    light_ubo.light_count = 0;
    light_ubo.has_dir_light = false;

    // ---------- Point / spot lights ----------
    const auto [lights, light_num] =
        light_processor.query_nearest_lights_limited<8>(
            camera_position /* или передавай явно */
        );

    light_ubo.light_count = light_num;

    for (uint32_t i = 0; i < light_num; ++i)
    {
        light_ubo.lights[i].position =
            glm::vec4(lights[i].position, 1.0f);

        light_ubo.lights[i].color = lights[i].color;
    }

    // ---------- Directional light ----------
    const auto dir_light =
        light_processor.get_directional_light();

    if (dir_light)
    {
        light_ubo.has_dir_light = true;

        light_ubo.dir_light.direction =
            glm::vec4(glm::normalize(dir_light->direction), 0.0f);

        light_ubo.dir_light.color = dir_light->color;

        light_ubo.dir_light.light_vp =
            build_dir_light_vp();
    }

    return light_ubo;
}

void GenericRenderGraph::prepare_geometry_batches(RenderGraphContext& ctx, Name pass_name)
{
    auto& scene_view = engine->scene_view;
    auto& meshes =
        scene_view->get_processor<SceneViewProcessor_Mesh>().meshes;

    auto& batches = prepared_batches[pass_name];
    batches.clear();

    std::unordered_map<ShaderKey, size_t> batch_lookup;

    for (const auto& ro : meshes)
    {
        for (uint32_t geom = 0;
             geom < ro.mesh.get().mesh_geometry.size();
             ++geom)
        {
            const auto& geometry = ro.mesh.get().mesh_geometry[geom];

            for (uint32_t prim = 0;
                 prim < geometry.primitives.size();
                 ++prim)
            {
                const auto& primitive = geometry.primitives[prim];
                
                
                ctx.backend.get_or_create_mesh_buffers(MeshPrimHandle{ro.mesh, geom, prim});
                uint32_t mat_index =
                    primitive.material_index.value_or(0);

                auto material = ro.mats[mat_index];

                Name blend_mode =
                    material->get_enum_parameter(Names::blend_mode);

                const bool should_draw =
                    (pass_name == Names::pass_geometry_base &&
                     blend_mode == Names::blend_mode_opaque) ||
                    (pass_name == Names::pass_geometry_translucent &&
                     blend_mode == Names::blend_mode_trasnlucent);

                if (!should_draw)
                    continue;

                auto model =
                    renderer->find_model(material->model);

                auto pipeline_family =
                    renderer->query_pipeline_family(
                        pass_name, model);

                ShaderKey key =
                    pipeline_family->make_shader_key(
                        material, pass_name);

                size_t batch_index;

                auto it = batch_lookup.find(key);
                if (it == batch_lookup.end())
                {
                    PipelineObject* pipeline =
                        pipeline_family->request_pipeline(key);

                    batch_index = batches.size();

                    batches.push_back({
                        .pipeline = pipeline,
                        .items = {}
                    });

                    batch_lookup[key] = batch_index;
                }
                else
                {
                    batch_index = it->second;
                }

                batches[batch_index].items.push_back({
                    .mesh     = MeshPrimHandle{ro.mesh, geom, prim},
                    .world    = ro.world,
                    .material = material
                });
                
                
            }
        }
    }
}

void GenericRenderGraph::prepare_geometry_resources(RenderGraphContext& ctx, Name pass_name)
{
    auto frame = ctx.frame;

    auto& batches = prepared_batches[pass_name];
    auto& pass_resources =
        prepared_pass_resources[pass_name];

    pass_resources.resize(num_pass_instances);

    for (uint32_t inst = 0; inst < num_pass_instances; ++inst)
    {
        auto& resource_map = pass_resources[inst];

        for (auto& batch : batches)
        {
            PipelineObject* pipeline = batch.pipeline;

            PreparedPassResources prep{};
            
            // ---------- Camera ----------
            CameraUBO cam_ubo = make_camera_ubo(ctx, false, inst);

            auto cam =
                camera_resource->query_single(
                    pipeline, inst);

            cam->update_uniform_buffer(
                pipeline, "camera_ubo", cam_ubo, frame);

            prep.camera = cam;
            
            
            // ---------- reflection capture -----
            
            auto refl = reflection_resource->query_single(pipeline);
            
            auto& scene_view = engine->scene_view;
            auto& reflection_capture_processor =
                scene_view->get_processor<SceneViewProcessor_ReflectionCapture>();
            
            auto reflection_capture = reflection_capture_processor.query_nearest(
                cam_ubo.camera_pos
            );
            if (reflection_capture)
            {
                refl->update_image(pipeline, "u_irradiance", renderer->get_cubemap(reflection_capture->irradiance), ctx.frame, 0, true);
                refl->update_image(pipeline, "u_prefilter_map", renderer->get_cubemap(reflection_capture->prefiltered_env), ctx.frame, 0, true);
                refl->update_image(pipeline, "u_brdf_lut", get_image(brdf_lut), ctx.frame);
            }
            
            prep.reflection = refl;
            


            // ---------- Lights ----------
            LightUBO light_ubo = build_light_ubo(cam_ubo.camera_pos);

            auto light =
                light_resource->query_single(pipeline);

            light->update_uniform_buffer(
                pipeline, "light_ubo", light_ubo, frame);

            prep.light = light;

            // ---------- Shadow ----------
            auto shadow =
                shadow_resource->query_single(pipeline);

            shadow->update_image(
                pipeline,
                "u_shadow_depth",
                get_image(shadow_map),
                frame);

            prep.shadow = shadow;

            resource_map[pipeline] = prep;
        }
    }
}

void GenericRenderGraph::prepare_shadow_pass(RenderGraphContext& ctx)
{
}

void GenericRenderGraph::prepare_clouds_pass(RenderGraphContext& ctx)
{
    auto frame = ctx.frame;

    static std::shared_ptr<Material> cloud_material;
    if (!cloud_material)
    {
        cloud_material = std::make_shared<Material>();
        cloud_material->model = "clouds";
    }

    auto cloud_model =
        renderer->find_model("clouds");

    auto pipeline_family =
        renderer->query_pipeline_family(
            "clouds",
            cloud_model
        );

    PipelineObject* pipeline =
        pipeline_family->request_pipeline({});

    prepared_clouds.pipeline = pipeline;

    // ---------- Camera ----------
    CameraUBO camera_ubo = make_camera_ubo(ctx);

    auto cam =
        camera_resource->query_single(pipeline);

    cam->update_uniform_buffer(
        pipeline,
        "camera_ubo",
        camera_ubo,
        frame
    );

    prepared_clouds.camera = cam;

    // ---------- Clouds UBO ----------
    CloudsUBO clouds_ubo{};
    
    clouds_ubo.planet_center = { 0, 0, 0, 0 };        // flat world
    clouds_ubo.cloud_base    = { 150.0f, 400.0f, 0.3f, 1.0f };

    clouds_ubo.sun_direction = { -0.4f, 0.8f, -0.3f, 0.0f };
    clouds_ubo.sun_color     = { 1.0f, 0.98f, 0.95f, 15.0f };

    clouds_ubo.cloud_color   = { 1.0f, 1.0f, 1.0f, 1.0f };

    clouds_ubo.scattering = { 1.0f, 0.4f, 0.25f, 0.0f };

    clouds_ubo.wind          = { 0.02f, 0.0f, 0.01f, 0.0 };
    
    clouds_ubo.sky_ambient = { 0.45f, 0.55f, 0.7f, 0.0f };
    clouds_ubo.horizon_color = { 0.8f, 0.9f, 1.0f, 0.0f };

    auto material_instance =
        renderer->query_material_instance(
            cloud_material,
            "Clouds"
        );

    auto* instance =
        material_instance->get_or_create_resource_instance(
            pipeline,
            frame
        );

    instance->update_uniform_buffer(
        pipeline,
        "clouds_ubo",
        clouds_ubo,
        frame
    );

    instance->update_image(
        pipeline,
        "u_depth",
        get_image(depth_texture),
        frame
    );

    instance->update_image(
        pipeline,
        "u_noise",
        get_image(noise_texture),
        frame
    );

    prepared_clouds.instance = instance;
}


bool GenericRenderGraph::is_debugging() const
{
    auto result = render_flags.find(Names::debug_shadow);
    return result != render_flags.end() && result->second;
}


glm::mat4 GenericRenderGraph::build_dir_light_vp() const
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


void GenericRenderGraph::draw_scene(RenderGraphContext& ctx)
{    
    PROFILE("draw_scene");
    
    auto cmd   = ctx.cmd;
    auto frame = ctx.frame;

    auto& batches =
        prepared_batches[ctx.pass_name];
    
    auto& resources_by_instance = prepared_pass_resources[ctx.pass_name];

    auto& pass_resources = resources_by_instance[ctx.level];

    for (auto& batch : batches)
    {
        PipelineObject* pipeline = batch.pipeline;
        auto& prep = pass_resources.at(pipeline);

        ctx.backend.bind_pipeline(cmd, pipeline);

        prep.camera->bind(pipeline, cmd, frame);
        prep.light->bind(pipeline, cmd, frame);
        prep.shadow->bind(pipeline, cmd, frame);
        prep.reflection->bind(pipeline, cmd, frame);

        for (auto& item : batch.items)
        {
            auto material_instance =
                renderer->query_material_instance(
                    item.material,
                    ctx.pass_name);

            material_instance->bind(pipeline, cmd, frame);

            ctx.backend.bind_mesh(cmd, item.mesh, frame);
            ctx.backend.push_constants(
                cmd, item.world, pipeline);

            ctx.backend.draw_indexed(
                cmd,
                item.mesh.get().indices.size(),
                RBDrawParams{
                    .update_viewport_extent = true,
                    .use_swapchain_extent = use_swapchain_extent,
                    .extent = resolution
                });
        }
    }
}

void GenericRenderGraph::draw_scene_shadow(RenderGraphContext& ctx)
{
    PROFILE("draw_scene_shadow");

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
            renderer->query_pipeline_family(
                "ShadowMap",
                renderer->find_model("Shadow")
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
                 .extent = Constants::shadowmap_extent
             }
        );
    }
}

void GenericRenderGraph::draw_clouds(RenderGraphContext& ctx, RGTextureHandle depth_texture, RGTextureHandle noise_texture)
{
    PROFILE("Clouds");
    
    
    auto cmd   = ctx.cmd;
    auto frame = ctx.frame;

    ctx.backend.bind_pipeline(
        cmd,
        prepared_clouds.pipeline
    );

    prepared_clouds.camera->bind(
        prepared_clouds.pipeline,
        cmd,
        frame
    );

    prepared_clouds.instance->bind(
        prepared_clouds.pipeline,
        cmd,
        frame
    );

    ctx.backend.draw_fullscreen(cmd);

}

CameraUBO GenericRenderGraph::make_camera_ubo(RenderGraphContext& ctx, bool zero_pos, uint32_t face_index) const
{
    auto& scene_view = engine->scene_view;
    CameraUBO camera_ubo;
    vec3 camera_position;
    
    // ---------- Camera ----------
    auto& camera_processor =
        scene_view->get_processor<SceneViewProcessor_Camera>();

    const RenderObject_Camera* cam_ro =
        camera_processor.get_active_camera();

    auto [width, height] = ctx.backend.get_viewport_extent();
        
    auto aspect = float(width) / float(height);
    auto proj = cam_ro->get_projection(aspect);
    auto view = cam_ro->view;
    auto fov = cam_ro->fov;
        
    if (zero_pos)
    {
        view[3][0] = 0.0f;
        view[3][1] = 0.0f;
        view[3][2] = 0.0f;
    }

    camera_ubo.proj = proj;
    camera_ubo.view = view;
    camera_ubo.camera_pos = cam_ro->position;
    camera_position = cam_ro->position;

    return camera_ubo;
}