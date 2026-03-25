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
import :debug_line;
import <set>;
import <algorithm>;

#include "common/assertion_macros.h"
#include "profiling/profile.h"

#include "common/assertion_macros.h"
#include "common/name_helpers.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

import log;

DEFINE_LOGGER(LogGenericRG, Display);


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
    shadow_resource = renderer->find_resource("shadow");
    reflection_resource = renderer->find_resource("reflection");
    gbuffer_resource = renderer->find_resource("gbuffer");
    ssr_resource = renderer->find_resource("ssr");
    hdr_color_output_resource = renderer->find_resource("hdr_color_output");
    hdr_color_storage_resource = renderer->find_resource("hdr_color_storage");
    tlas_resource = renderer->find_resource("tlas");
    mesh_table_resource = renderer->find_resource("mesh_table");
    clouds_resource = renderer->find_resource("clouds");
    base_color_resource = renderer->find_resource("base_color");
    pbr_material_table_resource = renderer->find_resource("pbr_material_table");
    textures_resource = renderer->find_resource("textures");
    primitive_table_resource = renderer->find_resource("primitive_table");
    
    
    
    auto& asset_manager = AssetManager::get();
    auto noise_tex = asset_manager.load_texture("textures/noise/noise_512.png");
    noise_texture = create_texture_from_asset(noise_tex, false);
    
    auto brdf_lut_tex = asset_manager.load_texture("textures/brdf_lut.png");
    brdf_lut = create_texture_from_asset(brdf_lut_tex, false);
    
    auto tonemap_model = renderer->find_model("tonemap");
    auto shadow_debug_model = renderer->find_model("shadow_debug");
    auto wireframe_model = renderer->find_model("wireframe");
    auto copy_model = renderer->find_model("copy");
    
    
    swapchain_extent = backend->get_swapchain_extent();

    const RenderResourceVariableDesc& hdr_color_variable = hdr_color_output_resource->find_var_checked("u_hdr_color");
    const uint16_t initial_array_size = hdr_color_variable.parameter.initial_array_size.value_or(1);
    
    hdr_color_table = create_textures({
        .name = NAME(hdr_color_table),
        .extent = resolution,
        .format = TextureFormat::RGBA16F,
        .usage  = 
             RenderTextureUsage::ColorAttachment | 
                RenderTextureUsage::Sampled | 
                RenderTextureUsage::TransferSrc | 
                RenderTextureUsage::TransferDst | 
                RenderTextureUsage::Storage,
        .external = false,
        .dimension = capture_dimension
    }, initial_array_size, reflect::enum_names<COLOR_OUTPUT>());

    std::vector<Name> color_outputs = reflect::enum_names<COLOR_OUTPUT>();
    check(color_outputs.size() <= initial_array_size);
    
    hdr_color_history = create_textures({
        .name = NAME(hdr_color_history),
        .extent = resolution,
        .format = TextureFormat::RGBA16F,
        .usage  = 
            RenderTextureUsage::ColorAttachment | 
            RenderTextureUsage::Sampled | 
            RenderTextureUsage::TransferSrc | 
            RenderTextureUsage::TransferDst | 
            RenderTextureUsage::Storage,
        .external = false,
        .dimension = capture_dimension,
        .num_layers = 2
    }, initial_array_size, color_outputs);
    
    
    const RenderResourceVariableDesc& gbuffer_resource_desc = gbuffer_resource->find_var_checked("u_gbuffer");
    
    std::vector<Name> gbuffer_slots = reflect::enum_names<GBUFFER_SLOTS>();
    
    check(gbuffer_slots.size() <= gbuffer_resource_desc.parameter.initial_array_size.value_or(0));
    
    
    gbuffer = create_textures({
        {
            .name = "g_normal",
            .extent = resolution,
            .format = TextureFormat::RGBA8_UNORM,
            .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .external = false,
            .dimension = capture_dimension
        },
        {
            .name = "g_world_normal",
            .extent = resolution,
            .format = TextureFormat::RGBA8_UNORM,
            .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .external = false,
            .dimension = capture_dimension
        },
        {
            .name = "g_roughness",
            .extent = resolution,
            .format = TextureFormat::R16F,
            .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .external = false,
            .dimension = capture_dimension
        },
        {
            .name = "g_depth",
            .extent = resolution,
            .format = TextureFormat::Depth24Stencil8,
            .usage = RenderTextureUsage::DepthStencil | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .dimension = capture_dimension
        },
        {
            .name = "g_albedo",
            .extent = resolution,
            .format = TextureFormat::RGBA8_UNORM,
            .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .external = false,
            .dimension = capture_dimension
        },
        {
            .name = "g_position",
            .extent = resolution,
            .format = TextureFormat::RGBA16F,
            .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .external = false,
            .dimension = capture_dimension
        },
        {
            .name = "g_motion_vectors",
            .extent = resolution,
            .format = TextureFormat::RG16F,
            .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .external = false,
            .dimension = capture_dimension
        }
    });
    
    gbuffer_hist = create_textures({
        {
            .name = "g_normal_hist",
            .extent = resolution,
            .format = TextureFormat::RGBA8_UNORM,
            .usage  = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .external = false,
            .dimension = capture_dimension,
            .num_layers = 2
        },
        {
            .name = "g_world_normal_hist",
            .extent = resolution,
            .format = TextureFormat::RGBA8_UNORM,
            .usage  = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .external = false,
            .dimension = capture_dimension,
            .num_layers = 2
        },
        {
            .name = "g_roughness_hist",
            .extent = resolution,
            .format = TextureFormat::R16F,
            .usage  = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .external = false,
            .dimension = capture_dimension,
            .num_layers = 2
        },
        {
            .name = "g_depth_hist",
            .extent = resolution,
            .format = TextureFormat::Depth24Stencil8,
            .usage = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .dimension = capture_dimension,
            .num_layers = 2
        },
        {
            .name = "g_albedo_hist",
            .extent = resolution,
            .format = TextureFormat::RGBA8_UNORM,
            .usage  = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .external = false,
            .dimension = capture_dimension,
            .num_layers = 2
        },
        {
            .name = "g_position_hist",
            .extent = resolution,
            .format = TextureFormat::RGBA16F,
            .usage  = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .external = false,
            .dimension = capture_dimension,
            .num_layers = 2
        },
        {
            .name = "g_motion_vectors_hist",
            .extent = resolution,
            .format = TextureFormat::RG16F,
            .usage  = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .external = false,
            .dimension = capture_dimension,
            .num_layers = 2
        }
    });
    
    
    
    ssr_texture = create_texture({
        .name = NAME(ssr_texture),
        .extent = resolution,
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
        .dimension = capture_dimension
    });
    
    tonemap_pipeline_family = renderer->query_pipeline_family("ToneMapping", tonemap_model);
    tonemap_pipeline = request_pipeline(tonemap_pipeline_family, {});
    
    wireframe_pipeline_family = renderer->query_pipeline_family("Wireframe", wireframe_model);
    wireframe_pipeline = request_pipeline(wireframe_pipeline_family, {});
    
    shadow_debug_pipeline_family = renderer->query_pipeline_family("ShadowDebug", shadow_debug_model);
    shadow_debug_pipeline = shadow_debug_pipeline_family->request_pipeline({});

    auto ssr_model = renderer->find_model("ssr");

    ssr_pipeline_family = renderer->query_pipeline_family("SSR", ssr_model);
    ssr_pipeline = ssr_pipeline_family->request_pipeline({});

    ssr_composite_pipeline_family = renderer->query_pipeline_family("SSRComposite", ssr_model);
    ssr_composite_pipeline = ssr_composite_pipeline_family->request_pipeline({});    
    
    auto rtxgi_model = renderer->find_model("rtxgi");
    
    rtx_gi_pipeline_family = renderer->query_pipeline_family("RTXGI", rtxgi_model);
    rtx_gi_pipeline = rtx_gi_pipeline_family->request_pipeline({});
    
    
    shadow_map = create_texture({
        .name = NAME(shadow_map),
        .extent = Constants::shadowmap_extent,
        .format = TextureFormat::Depth32F,
        .usage = RenderTextureUsage::DepthStencil | RenderTextureUsage::Sampled
    });
    
    
    
    debug_line_buffer = backend->create_vertex_buffer(VertexBufferDesc {
        .frame_size  = debug_line_capacity * sizeof(LineVertex),
        .dynamic = true
    });
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
                base_color_resource->update_image("u_base_color", get_image(shadow_map),
                    {
                    .frame = ctx.frame
                    });
                if (ctx.bind_pipeline(shadow_debug_pipeline))
                {
                    ctx.bind(base_color_resource);
                }
                ctx.draw_fullscreen();
           },
       });
    }
    
    
    
    swapchain_color = create_texture({
        .name = "swapchain",
        .extent = swapchain_extent,
        .format = backend->get_swapchain_format(),
        .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Present,
        .external = true,
        .swapchain_image = true,
    });
    
    // add_pass({
    //     .name = "DepthPrepass",
    //     .reads = {
    //     },
    //     .writes = {
    //         { depth_texture, RBImageUsage::DepthStencilAttachment, RBLoadOp::Clear },
    //     },
    //     .execute = [this](RenderGraphContext& ctx)
    //     {
    //         PROFILE("DepthPrepass");
    //         draw_scene(ctx);
    //     },
    //     .num_layers = 1
    // });

    
    add_pass({
        .name = Names::pass_geometry_base,
        .reads = {
            {  shadow_map, RBImageUsage::SampledFragment },
            { brdf_lut, RBImageUsage::SampledFragment, RBLoadOp::Load },
        },
        .writes = { 
            { hdr_color_table[COLOR_OUTPUT_HDR_BASE], RBImageUsage::ColorAttachment, RBLoadOp::Clear },
            { gbuffer[GBUFFER_SLOT_DEPTH], RBImageUsage::DepthStencilAttachment, RBLoadOp::Clear },
            { gbuffer[GBUFFER_SLOT_NORMAL], RBImageUsage::ColorAttachment, RBLoadOp::Clear },
            { gbuffer[GBUFFER_SLOT_WORLD_NORMAL], RBImageUsage::ColorAttachment, RBLoadOp::Clear },
            { gbuffer[GBUFFER_SLOT_MOTION_VECTORS], RBImageUsage::ColorAttachment, RBLoadOp::Clear },
            { gbuffer[GBUFFER_SLOT_ROUGHNESS], RBImageUsage::ColorAttachment, RBLoadOp::Clear },
            { gbuffer[GBUFFER_SLOT_ALBEDO], RBImageUsage::ColorAttachment, RBLoadOp::Clear },
            { gbuffer[GBUFFER_SLOT_POSITION], RBImageUsage::ColorAttachment, RBLoadOp::Clear }
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
            { hdr_color_table[COLOR_OUTPUT_HDR_BASE], RBImageUsage::ColorAttachment, RBLoadOp::Load },  
             { gbuffer[GBUFFER_SLOT_DEPTH], RBImageUsage::DepthStencilAttachment, RBLoadOp::Load },
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            PROFILE("Translucent");
            
            draw_scene(ctx);
        },
        .num_layers = num_pass_instances
    });
    
    
    
    add_pass({
        .name = "RTXGI",
        .reads = {
            { gbuffer[GBUFFER_SLOT_DEPTH], RBImageUsage::SampledFragment },
            { gbuffer[GBUFFER_SLOT_NORMAL], RBImageUsage::SampledFragment },
            { gbuffer[GBUFFER_SLOT_WORLD_NORMAL], RBImageUsage::SampledFragment },
            { gbuffer[GBUFFER_SLOT_ALBEDO], RBImageUsage::SampledFragment },
            { gbuffer[GBUFFER_SLOT_POSITION], RBImageUsage::SampledFragment },
        },
        .writes = {
            { hdr_color_table[COLOR_OUTPUT_HDR_RTXGI], RBImageUsage::StorageImage }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            draw_rtxgi(ctx);
        },
        .type = RenderPassType::rtx
    });
    
    
    
    add_pass({
        .name = "Clouds",
        .reads = {
            { gbuffer[GBUFFER_SLOT_DEPTH], RBImageUsage::SampledFragment },
            { noise_texture, RBImageUsage::SampledFragment }
        },
        .writes = {
            { hdr_color_table[COLOR_OUTPUT_HDR_BASE], RBImageUsage::ColorAttachment, RBLoadOp::Load },
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            PROFILE("Clouds");
            
            draw_clouds(ctx, gbuffer[GBUFFER_SLOT_DEPTH], noise_texture);
        },
        .num_layers = num_pass_instances
        
    });
    
    
    add_pass({
        .name = "SSR",
        .reads = {
            { hdr_color_table[COLOR_OUTPUT_HDR_BASE], RBImageUsage::SampledFragment },
            { gbuffer[GBUFFER_SLOT_DEPTH], RBImageUsage::SampledFragment },
            { gbuffer[GBUFFER_SLOT_NORMAL], RBImageUsage::SampledFragment },
            { gbuffer[GBUFFER_SLOT_ROUGHNESS], RBImageUsage::SampledFragment },
        },
        .writes = {
            { ssr_texture, RBImageUsage::ColorAttachment, RBLoadOp::Clear }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            draw_ssr(ctx);
        },
    });
    
    
    
    add_pass({
        .name = "SSRComposite",
        .reads = {
            { hdr_color_table[COLOR_OUTPUT_HDR_BASE], RBImageUsage::SampledFragment },
            { ssr_texture, RBImageUsage::SampledFragment },
            { gbuffer[GBUFFER_SLOT_ROUGHNESS], RBImageUsage::SampledFragment }
        },
        .writes = {
            { hdr_color_table[COLOR_OUTPUT_HDR_INTERMEDIATE], RBImageUsage::ColorAttachment, RBLoadOp::Load }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            draw_ssr_composite(ctx);
        },
    });
    
    add_pass({
        .name = "COPY_ssr_compose_to_hdr_base",
        .reads = {
            { hdr_color_table[COLOR_OUTPUT_HDR_INTERMEDIATE], RBImageUsage::TransferSrc }
        },
        .writes = {
            { hdr_color_table[COLOR_OUTPUT_HDR_BASE], RBImageUsage::TransferDst, RBLoadOp::Load }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            CopyImageParams params;
            params.source = get_image(hdr_color_table[COLOR_OUTPUT_HDR_INTERMEDIATE]);
            params.dest = get_image(hdr_color_table[COLOR_OUTPUT_HDR_BASE]);
            ctx.copy_img(params);
        },
        .num_layers = 1,
        .type = RenderPassType::transfer
    });
    
    
    add_pass({
        .name = "COPY_hdr_color_rtxgi_to_history",
        .reads = {
            { hdr_color_table[COLOR_OUTPUT_HDR_RTXGI], RBImageUsage::TransferSrc }
        },
        .writes = {
            { hdr_color_history[COLOR_OUTPUT_HDR_RTXGI], RBImageUsage::TransferDst, RBLoadOp::Load }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            if (ctx.level != history_index)
                return;
    
            CopyImageParams params;
            params.source = get_image(hdr_color_table[COLOR_OUTPUT_HDR_RTXGI]);
            params.dest = get_image(hdr_color_history[COLOR_OUTPUT_HDR_RTXGI]);
            params.dst_layer = ctx.level;
            
            ctx.copy_img(params);
        },
        .num_layers = 2,
        .type = RenderPassType::transfer
    });
    
    
    add_pass({
        .name = "COPY_gbuffer_world_normal_to_history",
        .reads = {
            { gbuffer[GBUFFER_SLOT_WORLD_NORMAL], RBImageUsage::TransferSrc }
        },
        .writes = {
            { gbuffer_hist[GBUFFER_SLOT_WORLD_NORMAL], RBImageUsage::TransferDst, RBLoadOp::Load }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            if (ctx.level != history_index)
                return;
    
            
            CopyImageParams params;
            params.source = get_image(gbuffer[GBUFFER_SLOT_WORLD_NORMAL]);
            params.dest = get_image(gbuffer_hist[GBUFFER_SLOT_WORLD_NORMAL]);
            params.dst_layer = ctx.level;
            ctx.copy_img(params);
        },
        .num_layers = 2,
        .type = RenderPassType::transfer
    });
    
    add_pass({
        .name = "COPY_gbuffer_depth_to_history",
        .reads = {
            { gbuffer[GBUFFER_SLOT_DEPTH], RBImageUsage::TransferSrc }
        },
        .writes = {
            { gbuffer_hist[GBUFFER_SLOT_DEPTH], RBImageUsage::TransferDst, RBLoadOp::Load }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            if (ctx.level != history_index)
                return;
            
            CopyImageParams params;
            params.source = get_image(gbuffer[GBUFFER_SLOT_DEPTH]);
            params.dest = get_image(gbuffer_hist[GBUFFER_SLOT_DEPTH]);
            params.dst_layer = ctx.level;
            ctx.copy_img(params);
        },
        .num_layers = 2,
        .type = RenderPassType::transfer
    });
    
    // add_pass({
    //     .name = "Wireframe",
    //     .reads = {
    //     },
    //     .writes = {
    //         { hdr_color, RBImageUsage::ColorAttachment, RBLoadOp::Load },
    //          { depth_texture, RBImageUsage::DepthStencilAttachment, RBLoadOp::Load },
    //     },
    //     .execute = [this](RenderGraphContext& ctx)
    //     {
    //         PROFILE("Wireframe");
    //         draw_wireframe(ctx);
    //     },
    //     .num_layers = 1
    // });
}

void GenericRenderGraph::prepare_resources(RenderGraphContext& ctx)
{
    RenderGraph::prepare_resources(ctx);
   
    rebuild_camera_ubo(ctx);
    
    prepared_batches.clear();

    // -------- resources --------
    prepare_geometry_resources(ctx);
    
    
    prepare_clouds_pass(ctx);
    prepare_wireframe_pass(ctx);
    prepare_ssr(ctx);
    prepare_raytracing(ctx);
}

void GenericRenderGraph::prepare_raytracing(RenderGraphContext& ctx)
{
    auto& mesh_processor =
       engine->scene_view->get_processor<SceneViewProcessor_Mesh>();
    
    if (mesh_processor.is_dirty())
    {
        std::vector<MeshPrimHandle> meshes;
        std::vector<Transform> transforms;

        meshes.reserve(mesh_processor.primitives.size());
        transforms.reserve(mesh_processor.primitives.size());

        for (auto& prim : mesh_processor.primitives)
        {
            if (!prim.passes.contains("GeometryTranslucent"))
            {
                meshes.push_back(prim.mesh);
                transforms.emplace_back(*prim.world);
            }
        }

        tlas = backend->build_tlas(
            ctx.cmd,
            meshes,
            transforms);
            
        auto mesh_table_info = backend->get_mesh_table_info();
        
        mesh_table_resource->update_ssbo(
            "u_mesh_table",
            sizeof(GPUMesh) * mesh_table_info.size,
            mesh_table_info.address,
            ctx.frame
        );
    
        mesh_processor.set_dirty(false);
    }
}

void GenericRenderGraph::end_frame()
{
    RenderGraph::end_frame();
    
    history_index ^= 1;
}

void GenericRenderGraph::rebuild_camera_ubo(RenderGraphContext& ctx)
{
    auto prev_proj = current_camera_ubo.proj;
    auto prev_view = current_camera_ubo.view;
    
    current_camera_ubo = make_camera_ubo(ctx, false, 0);
    current_camera_ubo.prev_proj = prev_proj;
    current_camera_ubo.prev_view = prev_view;
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
            camera_position
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

void GenericRenderGraph::bind_shadow_globals(
    RenderGraphContext& ctx)
{
    auto& scene_view = engine->scene_view;
    auto frame = ctx.frame;
    
    auto& camera_processor = scene_view->get_processor<SceneViewProcessor_Camera>();
    
    auto light_ubo = build_light_ubo(camera_processor.get_active_camera()->position);

    light_resource->update_uniform_buffer(
        "light_ubo",
        light_ubo,
        frame);
    
    ctx.bind(light_resource, mesh_table_resource, primitive_table_resource);
}


void GenericRenderGraph::prepare_geometry_resources(
    RenderGraphContext& ctx)
{
    PROFILE("prepare_geometry_resources");

    auto frame = ctx.frame;

    auto& mesh_processor = engine->scene_view->get_processor<SceneViewProcessor_Mesh>();
    
    // ----------------------------------------
    // Collect unique pipelines
    // ----------------------------------------

    std::map<RBPipelineLayout, Name> unique_pipelines;
    
    for (Name pass_name : 
        {
            Names::pass_geometry_base,
            Names::pass_geometry_translucent, 
            Name("DepthPrepass")
        })
    {
        for (auto& prim : mesh_processor.primitives)
        {
            if (auto info = prim.get_pass_info(pass_name))
                unique_pipelines.insert({info->pipeline_family->get_pipeline_layout(), pass_name});
        }
    }
    // ----------------------------------------
    // Prepare resources per pipeline
    // ----------------------------------------

    for (auto& [pipeline, pass_name] : unique_pipelines)
    {
        bool is_depth_prepass = (pass_name == Name("DepthPrepass"));
        

        // ---------- Camera ----------

        camera_resource->update_uniform_buffer(
            "camera_ubo",
            current_camera_ubo,
            frame);
        
        if (!is_depth_prepass)
        {
            // ---------- Reflection ----------

            auto& reflection_capture_processor = engine->scene_view->get_processor<SceneViewProcessor_ReflectionCapture>();

            auto reflection_capture =
                reflection_capture_processor.query_nearest(
                    current_camera_ubo.camera_pos);

            if (reflection_capture)
            {
                reflection_resource->update_image(
                    "u_irradiance",
                    renderer->get_cubemap(reflection_capture->irradiance),
                    {
                        .frame = frame,
                        .cubemap = true
                    });

                reflection_resource->update_image(
                    "u_prefilter_map",
                    renderer->get_cubemap(reflection_capture->prefiltered_env),
                    {
                        .frame = ctx.frame,
                        .cubemap = true
                    });

                reflection_resource->update_image(
                    "u_brdf_lut",
                    get_image(brdf_lut),
                    {
                        .frame = ctx.frame
                    });
            }


            // ---------- Lights ----------

            LightUBO light_ubo = build_light_ubo(current_camera_ubo.camera_pos);

            light_resource->update_uniform_buffer(
                "light_ubo",
                light_ubo,
                frame);

            // ---------- Shadow ----------

            shadow_resource->update_image(
                "u_shadow_depth",
                get_image(shadow_map),
            {
                    .frame = ctx.frame
                });

        }
        
        // resource_map[pipeline] = prep;
    }
}


void GenericRenderGraph::prepare_shadow_pass(RenderGraphContext& ctx)
{
}

void GenericRenderGraph::prepare_clouds_pass(RenderGraphContext& ctx)
{
    PROFILE("prepare_clouds_pass");
    auto frame = ctx.frame;

    auto cloud_model =
        renderer->find_model("clouds");

    auto pipeline_family =
        renderer->query_pipeline_family(
            "clouds",
            cloud_model
        );

    clouds_pipeline = pipeline_family->request_pipeline({});

    // ---------- Camera ----------
    camera_resource->update_uniform_buffer(
        "camera_ubo",
        current_camera_ubo,
        frame
    );

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
    
    clouds_resource->update_uniform_buffer(
        "clouds_ubo",
        clouds_ubo,
        frame
    );

    clouds_resource->update_image(
        "u_depth",
        get_image(gbuffer[GBUFFER_SLOT_DEPTH]),
        {
            .frame = ctx.frame
          }
    );

    clouds_resource->update_image(
        "u_noise",
        get_image(noise_texture),
        {
            .frame = ctx.frame
        }
    );

}

void GenericRenderGraph::prepare_wireframe_pass(RenderGraphContext& ctx)
{
    auto frame = ctx.frame;

    camera_resource->update_uniform_buffer(
        "camera_ubo",
        current_camera_ubo,
        frame
    );
}

void GenericRenderGraph::prepare_ssr(RenderGraphContext& ctx)
{
    
    setup_hdr_color_table(ctx);

    
    hdr_color_output_resource->update_image(
        "u_history",
        get_image(hdr_color_table[COLOR_OUTPUT_HDR_BASE]),
        {.frame=ctx.frame});

    ssr_resource->update_image(
        "u_ssr",
        get_image(ssr_texture),
        {.frame=ctx.frame});
    
}

void GenericRenderGraph::setup_hdr_color_table(RenderGraphContext& ctx) const
{
    for (uint16_t index = 0; index < hdr_color_table.size(); ++index)
    {
        hdr_color_output_resource->update_image(
            "u_hdr_color",
            get_image(hdr_color_table[index]),
            {
                .frame=ctx.frame,
                .array_index = index
            });
        hdr_color_storage_resource->update_image(
            "u_hdr_color",
            get_image(hdr_color_table[index]),
            {
                .frame=ctx.frame,
                .array_index = index
            });
    }
    
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

    glm::vec3 minLS(FLT_MAX);
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
    PROFILE("GenericRenderGraph::draw_scene");

    auto& mesh_processor =
        engine->scene_view
            ->get_processor<SceneViewProcessor_Mesh>();

    ctx.backend.update_viewport(
        ctx.cmd,
        resolution,
        use_swapchain_extent);
    
    bool is_depth_prepass = (ctx.pass_name == Name("DepthPrepass"));

    std::vector<ViewRenderItem> items;
    
    auto view_info = build_view_info(ctx, false);

    mesh_processor.gather_for_view(
        view_info.view,
        view_info.frustum,
        ctx.pass_name,
        items);

    if (ctx.pass_name == Name("DepthPrepass"))
    {
        PROFILE("draw_scene - sort");
        std::sort(items.begin(), items.end(),
            [](const ViewRenderItem& a,
               const ViewRenderItem& b)
            {
                return a.get_sort_key() < b.get_sort_key();
            });
    }
    
    
    auto pbr = renderer->find_model("pbr");
    std::shared_ptr<PipelineFamily> family = renderer->query_pipeline_family(ctx.pass_name, pbr);
    
    PROFILE("GenericRenderGraph::draw_scene - items");
    for (auto& item : items)
    {
        const RenderPrimitive& prim = *item.primitive;
    
        const RenderPrimitivePassInfo* prim_info = prim.get_pass_info(ctx.pass_name);
        
        if (!prim_info)
            continue;
    
        PipelineObject* pipeline = prim_info->pipeline;
    
        if (ctx.bind_pipeline(pipeline))
        {
            ctx.bind(camera_resource, mesh_table_resource,
                     shadow_resource, light_resource, reflection_resource, pbr_material_table_resource, textures_resource,
                     primitive_table_resource);
        }       
        
        ModelPushConstants pc;
        pc.mesh_id = prim.mesh_index;
        pc.primitive_id = prim.id;
        pc.material_id = prim_info->material_index;
        pc.debug_id = 0;
        
        ctx.push_constants(pc);
        
        ctx.draw(prim.mesh.get().indices.size());
    }
    
}


void GenericRenderGraph::draw_scene_shadow(RenderGraphContext& ctx)
{
    PROFILE("draw_scene_shadow");
    
    auto& mesh_processor = engine->scene_view->get_processor<SceneViewProcessor_Mesh>();

    ctx.backend.update_viewport(ctx.cmd, Constants::shadowmap_extent);

    for (auto& prim : mesh_processor.primitives)
    {
        const RenderPrimitivePassInfo* info = prim.get_pass_info(ctx.pass_name);  // todo: temp crutch (provide shadow pass instead)
        
        if (!info)
            continue;
        
        auto pipeline = info->pipeline;
        
        if (ctx.bind_pipeline(pipeline))
        {
            bind_shadow_globals(ctx);
        }
        
        
        ModelPushConstants pc;
        pc.mesh_id = prim.mesh_index;
        pc.primitive_id = prim.id;
        pc.material_id = 0;
        pc.debug_id = 0;
        ctx.push_constants(pc);
        
        ctx.draw(prim.mesh.get().indices.size());
    }
}

void GenericRenderGraph::draw_clouds(RenderGraphContext& ctx, RGTextureHandle depth_texture, RGTextureHandle noise_texture)
{
    PROFILE("Clouds");
    
    if (ctx.bind_pipeline(clouds_pipeline))
    {
        ctx.bind(camera_resource, clouds_resource);
    }
    ctx.draw_fullscreen();

}


void add_aabb_lines(
    const AABB& box,
    const glm::vec4& color,
    std::vector<DebugLine>& out_lines)
{
    glm::vec3 c[8];
    box.get_corners(c);

    out_lines.push_back({ c[0], c[1], color });
    out_lines.push_back({ c[1], c[3], color });
    out_lines.push_back({ c[3], c[2], color });
    out_lines.push_back({ c[2], c[0], color });

    out_lines.push_back({ c[4], c[5], color });
    out_lines.push_back({ c[5], c[7], color });
    out_lines.push_back({ c[7], c[6], color });
    out_lines.push_back({ c[6], c[4], color });

    out_lines.push_back({ c[0], c[4], color });
    out_lines.push_back({ c[1], c[5], color });
    out_lines.push_back({ c[2], c[6], color });
    out_lines.push_back({ c[3], c[7], color });
}


void GenericRenderGraph::draw_wireframe(RenderGraphContext& ctx)
{
    auto cmd   = ctx.cmd;
    auto frame = ctx.frame;

    auto& debug_processor =
        engine->scene_view
            ->get_processor<SceneViewProcessor_Mesh>();
    
    ctx.bind_pipeline(wireframe_pipeline);
    
    ctx.bind(camera_resource);

    const auto& primitives = debug_processor.primitives; 
    
    if (primitives.empty())
        return;

    std::vector<DebugLine> lines;

    int32_t index = 0;
    for (const auto& prim : primitives)
    {
        add_aabb_lines(prim.bounds, vec4(1.0, 0.0, 0.0, 1.0), lines);
        index++;
        
    }

    if (lines.empty())
        return;

    const uint32_t vertex_count =
        static_cast<uint32_t>(lines.size()) * 2;
    
    checkf(vertex_count * sizeof(LineVertex) <= debug_line_capacity,
        "vertex count too large");

    std::vector<LineVertex> vertices;
    vertices.reserve(vertex_count);

    for (const DebugLine& l : lines)
    {
        vertices.push_back({
            l.a,
            0.0f,
            l.color
        });

        vertices.push_back({
            l.b,
            0.0f,
            l.color
        });
    }

    auto* dst = static_cast<LineVertex*>(
        backend->get_vertex_buffer_ptr(
            debug_line_buffer,
            frame));

    memcpy(dst,
           vertices.data(),
           vertex_count * sizeof(LineVertex));


    backend->bind_vertex_buffer(
        cmd,
        debug_line_buffer,
        frame);


    backend->draw(
        cmd,
        vertex_count);
}

void GenericRenderGraph::draw_ssr(RenderGraphContext& ctx)
{
    PROFILE("GenericRenderGraph::draw_ssr");

    ctx.backend.update_viewport(
        ctx.cmd,
        resolution,
        use_swapchain_extent);
    
    if (ctx.bind_pipeline(ssr_pipeline))
    {
        ctx.bind(camera_resource, gbuffer_resource, hdr_color_output_resource);
    }
    
    ctx.backend.draw_fullscreen(ctx.cmd);
}

void GenericRenderGraph::draw_ssr_composite(RenderGraphContext& ctx)
{
    PROFILE("GenericRenderGraph::draw_ssr_composite");

    ctx.backend.update_viewport(
        ctx.cmd,
        resolution,
        use_swapchain_extent);

    if (ctx.bind_pipeline(ssr_composite_pipeline))
    {
        ctx.bind(gbuffer_resource, hdr_color_output_resource, ssr_resource);
    }
    ctx.backend.draw_fullscreen(ctx.cmd);
}

void GenericRenderGraph::draw_rtxgi(RenderGraphContext& ctx)
{
    ctx.bind_pipeline(rtx_gi_pipeline);

    setup_hdr_color_table(ctx);

    tlas_resource->update_tlas(
        "u_tlas",
        tlas,
        ctx.frame);
    
    ctx.bind(
        hdr_color_storage_resource,
        tlas_resource,
        gbuffer_resource,
        camera_resource,
        light_resource,
        mesh_table_resource,
        pbr_material_table_resource,
        textures_resource,
        primitive_table_resource);
    
    RTXGIPushConstants pc{};
    pc.frame = frame_index;
    pc.intensity = 1.0f;

    ctx.backend.push_constants(
        ctx.cmd,
        pc
    );

    ctx.backend.trace_rays(
        ctx.cmd,
        rtx_gi_pipeline,
        {resolution.width, resolution.height},
        1
    );
}


ViewInfo GenericRenderGraph::build_view_info(RenderGraphContext& ctx, bool zero_pos) const
{
    auto& scene_view = engine->scene_view;

    auto& camera_processor =
        scene_view->get_processor<SceneViewProcessor_Camera>();

    const RenderObject_Camera* cam_ro =
        camera_processor.get_active_camera();

    auto [width, height] =
        ctx.backend.get_viewport_extent();

    float aspect =
        float(width) / float(height);

    glm::mat4 proj =
        cam_ro->get_projection(aspect);

    glm::mat4 view =
        cam_ro->view;

    if (zero_pos)
    {
        view[3][0] = 0.0f;
        view[3][1] = 0.0f;
        view[3][2] = 0.0f;
    }

    glm::mat4 vp = proj * view;

    ViewInfo info;
    info.view = view;
    info.proj = proj;
    info.frustum = Frustum::from_view_projection(vp);

    return info;
}

CameraUBO GenericRenderGraph::make_camera_ubo(RenderGraphContext& ctx, bool zero_pos, uint32_t face_index) const
{
    auto& scene_view = engine->scene_view;
    CameraUBO camera_ubo;

    // ---------- Camera ----------
    auto& camera_processor =
        scene_view->get_processor<SceneViewProcessor_Camera>();

    const RenderObject_Camera* cam_ro =
        camera_processor.get_active_camera();

    auto [width, height] = backend->get_viewport_extent();
        
    auto aspect = float(width) / float(height);
    auto proj = cam_ro->get_projection(aspect);
    auto view = cam_ro->view;
        
    if (zero_pos)
    {
        view[3][0] = 0.0f;
        view[3][1] = 0.0f;
        view[3][2] = 0.0f;
    }

    camera_ubo.proj = proj;
    camera_ubo.view = view;
    camera_ubo.inv_proj = glm::inverse(proj);
    camera_ubo.inv_view = glm::inverse(view);
    camera_ubo.inv_viewproj = glm::inverse(proj * view);
    camera_ubo.camera_pos = cam_ro->position;
    camera_ubo.far = cam_ro->far;
    camera_ubo.near = cam_ro->near;

    return camera_ubo;
}
