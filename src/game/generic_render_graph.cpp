module;

#include <cfloat>

module game;

import :generic_render_graph;

import std.compat;
import assertions;
import fixed_string;


import render;
import vk;
import glm;
import rhmath;
import profile;
import name;
import rhcomponents;
import globals;
import assets;
import texture_format;
import :constants;
import :names;
import :debug_line;
import debug_draw;
import dump_exr;
import paths;
import platform;

import log;
#include "common/assertion_macros.h"
#include "profiling/profile.h"

#include "common/assertion_macros.h"
#include "common/name_helpers.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"


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
    
    fetch_declared_resource();

    
    auto& asset_manager = AssetManager::get();
    
    auto brdf_lut_tex = asset_manager.load_texture("textures/brdf_lut.png");
    brdf_lut = create_texture_from_asset(brdf_lut_tex, false);
    
    auto tonemap_model = renderer->find_model("tonemap");
    auto shadow_debug_model = renderer->find_model("shadow_debug");
    auto wireframe_model = renderer->find_model("wireframe");
    
    
    swapchain_extent = backend->get_swapchain_extent();

    const RenderResourceVariableDesc& hdr_color_variable = hdr_color_output_resource->find_var_checked("u_hdr_color_present");
    const uint16_t initial_array_size = hdr_color_variable.parameter.initial_array_size.value_or(1);
    
    hdr_color_present = create_textures({
        .name = NAME(hdr_color_present),
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
    }, initial_array_size, reflect::enum_names<COLOR_OUTPUT_HDR>());

    std::vector<Name> color_outputs = reflect::enum_names<COLOR_OUTPUT_HDR>();
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
    
    decal_albedo = create_texture({
            .name = "decal_albedo",
            .extent = resolution,
            .format = TextureFormat::RGBA8_UNORM,
            .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .external = false,
            .dimension = capture_dimension
        });
    
    gbuffer = create_textures({
        {
            // not written any more (view normals come from the world normal): 1 texel keeps the slot numbering
            .name = "g_normal",
            .extent = { 1, 1 },
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
            .name = "g_depth",
            .extent = resolution,
            .format = TextureFormat::Depth24Stencil8,
            .usage = RenderTextureUsage::DepthStencil | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .dimension = capture_dimension
        },
        {
            .name = "g_linear_depth",
            .extent = resolution,
            .format = TextureFormat::R32F,
            .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
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
            // not written any more: shaders take the position from the linear depth (utils/gbuffer_position.glsl,
            // float: sub-millimeter, what the shadow normal offset needs, Rhea-xfu). 1 texel keeps the slot numbering
            .name = "g_position",
            .extent = { 1, 1 },
            .format = TextureFormat::RGBA32F,
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
        },
        {
            .name = "g_geometry_normal",
            .extent = resolution,
            .format = TextureFormat::RGBA8_UNORM,
            .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .external = false,
            .dimension = capture_dimension
        },
        {
            .name = "g_emissive",
            .extent = resolution,
            // HDR emissive (RGBA8 clamped it to 1). As RGBA8 (the 8th color target) its B/A bytes intermittently
            // came back as 4x8 texel blocks of garbage under heavy load on an RTX 4080 Laptop, with correct
            // shaders and full GPU serialization: compression / driver level, see memory notes
            .format = TextureFormat::RGBA16F,
            .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            .external = false,
            .dimension = capture_dimension
        },
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
            .name = "g_depth_hist",
            .extent = resolution,
            .format = TextureFormat::Depth24Stencil8,
            .usage = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .dimension = capture_dimension,
            .num_layers = 2
        },
        {
            .name = "g_linear_depth_hist",
            .extent = resolution,
            .format = TextureFormat::R32F,
            .usage = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .dimension = capture_dimension,
            .num_layers = 2
        },
        {
            .name = "g_albedo_roughness_hist",
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
            // same format as g_position (history copy)
            .format = TextureFormat::RGBA32F,
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
        },
        {
            .name = "g_geometry_normal_hist",
            .extent = resolution,
            .format = TextureFormat::RGBA8_UNORM,
            .usage  = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .external = false,
            .dimension = capture_dimension,
            .num_layers = 2
        },
        
        {
            .name = "g_emissive_hist",
            .extent = resolution,
            .format = TextureFormat::RGBA16F,
            .usage  = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .external = false,
            .dimension = capture_dimension,
            .num_layers = 2
        },
    });
    
    
    
    if (emissive_diagnostics_enabled())
    emissive_ring = create_texture({
        .name = "g_emissive_ring",
        .extent = resolution,
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst | RenderTextureUsage::TransferSrc,
        .external = false,
        .dimension = capture_dimension,
        .num_layers = EMISSIVE_RING_SIZE
    });
    
    // screen space reflections at 1 / ssr_downscale of the screen (main graph): SSRComposite upsamples them
    ssr_downscale = resolution.is_zero() ? uint32_t(std::clamp(cv_ssr_downscale.get(), 1, 4)) : 1;
    ssr_texture = create_texture({
        .name = NAME(ssr_texture),
        .extent = resolution,
        .extent_divisor = resolution.is_zero() ? ssr_downscale : 0,
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
        .dimension = capture_dimension
    });
    
    
    auto pbr_model = renderer->find_model("pbr");
    lighting_pipeline_family = renderer->query_pipeline_family("Lighting", pbr_model);
    
    tonemap_pipeline_family = renderer->query_pipeline_family("ToneMapping", tonemap_model);
    
    wireframe_pipeline_family = renderer->query_pipeline_family("Wireframe", wireframe_model);
    wireframe_mesh_pipeline_family = renderer->query_pipeline_family("WireframeMesh", wireframe_model);
    skeleton_pipeline_family = renderer->query_pipeline_family("SkeletonOverlay", wireframe_model);
    debug_lines_pipeline_family = renderer->query_pipeline_family("DebugLines", wireframe_model);
    
    shadow_debug_pipeline_family = renderer->query_pipeline_family("ShadowDebug", shadow_debug_model);

    auto ssr_model = renderer->find_model("ssr");

    ssr_pipeline_family = renderer->query_pipeline_family("SSR", ssr_model);

    ssr_composite_pipeline_family = renderer->query_pipeline_family("SSRComposite", ssr_model); 
    
    if constexpr (render_settings::enable_raytracing)
    {
        auto rtxgi_model = renderer->find_model("rtxgi");
        
        rtx_gi_pipeline_family = renderer->query_pipeline_family("RTXGI", rtxgi_model);
        
        rtx_gi_reproject_pipeline_family = renderer->query_pipeline_family("RTXGI_REPROJECT", rtxgi_model);
        
        rtx_gi_temporal_accum_pipeline_family = renderer->query_pipeline_family("RTXGI_TEMPORAL_ACCUM", rtxgi_model);
        
        rtx_gi_moments_pipeline_family = renderer->query_pipeline_family("RTXGI_MOMENTS", rtxgi_model);
        
        rtx_gi_spatial_filter_pipeline_family = renderer->query_pipeline_family("RTXGI_SPATIAL_FILTER", rtxgi_model);
    }
    
    auto skinning_model = renderer->find_model("skinning");
    skinning_pipeline_family = renderer->query_pipeline_family("Skinning", skinning_model);
    
    
    shadow_map = create_texture({
        .name = NAME(shadow_map),
        .extent = Constants::shadowmap_extent,
        .format = TextureFormat::Depth32F,
        .usage = RenderTextureUsage::DepthStencil | RenderTextureUsage::Sampled
    });

    // what the clouds let through of the directional light (SkyRenderer, pass "CloudShadow"); never written
    // and never read (strength 0 in the light UBO) by a graph without a sky
    cloud_shadow_map = create_texture({
        .name = NAME(cloud_shadow_map),
        .extent = { SkyRenderer::cloud_shadow_texels, SkyRenderer::cloud_shadow_texels },
        .format = TextureFormat::R16F,
        .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled
    });
    
    
    debug_line_buffer = backend->create_vertex_buffer(VertexBufferDesc {
        .frame_size  = static_cast<uint32_t>(debug_line_capacity * sizeof(LineVertex)),
        .dynamic = true
    });
    
    
    draw_list.init(*renderer, *backend);

    if (num_pass_instances == 1)
    {
        reflection_probes = std::make_unique<ReflectionProbeSystem>();
        reflection_probes->init(*renderer, *backend);

        sky = std::make_unique<SkyRenderer>();
        sky->init(*renderer, *backend);

        particles = std::make_unique<ParticleRenderer>();
        particles->init(*renderer, *backend);

        occlusion = std::make_unique<OcclusionCulling>();
        occlusion->init(*renderer, *backend);

        foliage = std::make_unique<FoliageRenderer>();
        foliage->init(*renderer, *backend);

        sky_downscale = uint32_t(std::clamp(cv_sky_downscale.get(), 1, 8));
        atmosphere_buffer = create_texture({
            .name = NAME(atmosphere_buffer),
            .extent_divisor = sky_downscale,
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
        });
        clouds_buffer = create_texture({
            .name = NAME(clouds_buffer),
            .extent_divisor = sky_downscale,
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
        });
        clouds_history = create_texture({
            .name = NAME(clouds_history),
            .extent_divisor = sky_downscale,
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .num_layers = 2
        });

        fog_downscale = uint32_t(std::clamp(cv_fog_downscale.get(), 1, 8));
        fog_buffer = create_texture({
            .name = NAME(fog_buffer),
            .extent_divisor = fog_downscale,
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
        });
        fog_history = create_texture({
            .name = NAME(fog_history),
            .extent_divisor = fog_downscale,
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst,
            .num_layers = 2
        });

        gtao = std::make_unique<GtaoRenderer>();
        gtao->init(*renderer, *backend);

        // rgba: visibility, linear depth, packed edges (resources/gtao.glsl)
        gtao_downscale = uint32_t(std::clamp(cv_gtao_downscale.get(), 1, 2));
        for (auto [handle, name] : { std::pair{ &gtao_raw, "gtao_raw" }, std::pair{ &gtao_mid, "gtao_mid" },
                 std::pair{ &gtao_result, "gtao_result" } })
        {
            *handle = create_texture({
                .name = name,
                .extent_divisor = gtao_downscale,
                .format = TextureFormat::RGBA16F,
                .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
            });
        }
    }
    else
    {
        // the lighting pass samples it, the push constants tell it not to
        gtao_result = create_texture({
            .name = NAME(gtao_result),
            .extent = { 1, 1 },
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
        });
    }

    if constexpr (render_settings::enable_nn_denoiser)
    {
        // nn_denoiser::add_nn_denoiser_passes(nn_denoiser_state, *this, *renderer);
        nn_denoiser::load_nn_denoiser_schemas(nn_denoiser_state);
        nn_denoiser::allocate_nn_gpu_resources(nn_denoiser_state, *this, *renderer, *backend, swapchain_extent);
    }
    
    on_pso_built();
}

void GenericRenderGraph::build_passes(const std::map<Name, bool>& parameters)
{
    // GPU skinning: must run before every consumer of skinned vertices
    // (shadow map, base pass, RT hit shaders)
    add_pass({
        .name = "Skinning",
        .execute = [this] (RenderGraphContext& ctx)
        {
            dispatch_skinning(ctx);
        },
        .type = RenderPassType::compute
    });
    
    add_pass({
        .name = "ShadowMap",
        .writes = {
            // time sliced cascades: tiles not rendered this frame are kept, rendered ones cleared by draw_scene_shadow
            { shadow_map, RBImageUsageType::DepthStencilAttachment, RBLoadOp::Load }
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            draw_scene_shadow(ctx);
        },
    });
    
    // shadow of the clouds on the scene, sampled with the shadow map (resources/shadow.glsl)
    if (sky)
    {
        add_pass({
            .name = "CloudShadow",
            .writes = {
                { cloud_shadow_map, RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("CloudShadow");
                sky->draw_cloud_shadow(ctx, light_resource);
                ctx.backend.update_viewport(ctx.cmd, resolution, use_swapchain_extent);
            },
        });
    }

    // time sliced probe baking: capture faces sample the shadow maps, the lighting samples the probes
    if (reflection_probes)
    {
        add_pass({
            .name = "ReflectionProbes",
            .reads = {
                { shadow_map, RBImageUsageType::SampledFragment },
                { cloud_shadow_map, RBImageUsageType::SampledFragment }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("ReflectionProbes");
                reflection_probes->execute(ctx, *engine->scene_view, draw_list);
            },
            // records its own render passes and barriers (capture cube faces, filtered probe slots)
            .type = RenderPassType::transfer
        });
    }
    
    if (allow_shadow_debug)
    {
        add_pass({
           .name = "ShadowDebug",
           .condition = [this] () { return renderer->get_render_flag(Names::debug_shadow); },
           .reads = {
               { shadow_map, RBImageUsageType::SampledFragment }
           },
           .writes = {
               { swapchain_color, RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
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
        .num_frames = (uint8_t)backend->get_num_images_in_flight()
    });
    
    // the g-buffer targets of the base pass: cleared by GeometryBase, loaded by GeometryBaseLate
    auto base_pass_targets = [this] (RBLoadOp load)
    {
        std::vector<RBImageUsage> targets;
        targets.push_back({ gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::DepthStencilAttachment, load });
        // attachment order = fragment output locations of the base pass shaders (geometry.frag, ...)
        for (GBUFFER_SLOTS slot : { GBUFFER_SLOTS::WORLD_NORMAL, GBUFFER_SLOTS::MOTION_VECTORS,
                                    GBUFFER_SLOTS::ALBEDO_ROUGHNESS, GBUFFER_SLOTS::LINEAR_DEPTH,
                                    GBUFFER_SLOTS::GEOMETRY_NORMAL, GBUFFER_SLOTS::EMISSIVE })
            targets.push_back({ gbuffer[slot], RBImageUsageType::ColorAttachment, load });
        return targets;
    };
    const auto culling = [this] () { return occlusion && cv_occlusion_culling.get(); };

    // occlusion culling, early phase: the draws visible at the end of the last frame
    if (occlusion)
    {
        add_pass({
            .name = "OcclusionCullEarly",
            .condition = culling,
            .execute = [this] (RenderGraphContext& ctx)
            {
                occlusion->cull(ctx, camera_resource, draw_list.get_resource(), false, base_first_command, base_command_count);
            },
            .type = RenderPassType::compute
        });
    }

    add_pass({
        .name = Names::pass_geometry_base,
        .reads = {
        },
        .writes = base_pass_targets(RBLoadOp::Clear),
        .execute = [this] (RenderGraphContext& ctx)
        {
            PROFILE("Geometry");
            
            draw_scene(ctx);
        },
        .num_layers = num_pass_instances
    });

    // occlusion culling, late phase: what the depth of the early draws does not hide, and was not drawn early
    if (occlusion)
    {
        add_pass({
            .name = "HZB",
            .condition = culling,
            .reads = {
                { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::Sampled }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                occlusion->build_hzb(ctx);
            },
            .type = RenderPassType::compute
        });

        add_pass({
            .name = "OcclusionCullLate",
            .condition = culling,
            .execute = [this] (RenderGraphContext& ctx)
            {
                occlusion->cull(ctx, camera_resource, draw_list.get_resource(), true, base_first_command, base_command_count);
            },
            .type = RenderPassType::compute
        });

        add_pass({
            .name = Names::pass_geometry_base_late,
            .condition = culling,
            .writes = base_pass_targets(RBLoadOp::Load),
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("GeometryLate");
                draw_scene(ctx);
            },
        });
    }
    
    // ground foliage: grown after the hierarchical depth of the early draws (culls it too), drawn over the base pass
    if (foliage)
    {
        add_pass({
            .name = "FoliageCull",
            .condition = [this] () { return foliage->is_active(); },
            .execute = [this] (RenderGraphContext& ctx)
            {
                foliage->cull(ctx, camera_resource, occlusion->get_resource());
            },
            .type = RenderPassType::compute
        });

        add_pass({
            .name = "GroundFoliage",
            .condition = [this] () { return foliage->is_active(); },
            .writes = base_pass_targets(RBLoadOp::Load),
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("GroundFoliage");
                ctx.backend.update_viewport(ctx.cmd, resolution, use_swapchain_extent);
                foliage->draw(ctx, {
                    .camera = camera_resource,
                    .mesh_table = mesh_table_resource,
                    .material_table = pbr_material_table_resource,
                    .textures = textures_resource,
                });
            },
        });
    }

    // emissive watch ring (see EMISSIVE_RING_SIZE): g_emissive as the base pass left it
    if (emissive_diagnostics_enabled())
    add_pass({
        .name = "COPY_gbuffer_emissive_to_watch_ring",
        .condition = [this] () { return emissive_watch_active; },
        .reads = {
            { gbuffer[GBUFFER_SLOTS::EMISSIVE], RBImageUsageType::TransferSrc }
        },
        .writes = {
            { emissive_ring, RBImageUsageType::TransferDst, RBLoadOp::Load }
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            const uint32_t layer = emissive_ring_next;
            emissive_ring_next = (layer + 1) % EMISSIVE_RING_SIZE;
            emissive_ring_layer_of_slot[ctx.frame] = layer;

            CopyImageParams params;
            params.source = get_image(gbuffer[GBUFFER_SLOTS::EMISSIVE]);
            params.dest = get_image(emissive_ring);
            params.dst_layer = layer;
            ctx.copy_img(params);
        },
        .num_layers = 1,
        .type = RenderPassType::transfer
    });
    
    
    
    
    // ---- ray traced GI: RTXGI -> SVGF (reproject, temporal, moments, spatial) -> NN denoiser ----
    if constexpr (render_settings::enable_raytracing)
    {
        add_pass({
            .name = "RTXGI",
            .reads = {
                { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::SampledFragment },
                { gbuffer[GBUFFER_SLOTS::NORMAL], RBImageUsageType::SampledFragment },
                { gbuffer[GBUFFER_SLOTS::WORLD_NORMAL], RBImageUsageType::SampledFragment },
                { gbuffer[GBUFFER_SLOTS::ALBEDO_ROUGHNESS], RBImageUsageType::SampledFragment },
                { gbuffer[GBUFFER_SLOTS::POSITION], RBImageUsageType::SampledFragment },
            },
            .writes = {
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI], RBImageUsageType::StorageImage }
            },
            .execute = [this](RenderGraphContext& ctx)
            {
                draw_rtxgi(ctx);
            },
            .type = RenderPassType::rtx
        });
    
    
    

    
        add_pass({
            .name = "RTXGI_REPROJECT",
            .reads = {
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI], RBImageUsageType::SampledFragment },
                { hdr_color_history[COLOR_OUTPUT_HDR::RTXGI_ACCUM], RBImageUsageType::SampledFragment },

                { gbuffer[GBUFFER_SLOTS::MOTION_VECTORS], RBImageUsageType::SampledFragment },
                { gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::SampledFragment },
                { gbuffer_hist[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::SampledFragment },
            },
            .writes = {
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_REPROJECTED], RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
            },
            .execute = [this](RenderGraphContext& ctx)
            {
                if (ctx.bind_pipeline(rtx_gi_reproject_pipeline))
                {
                    ctx.bind(hdr_color_output_resource, hdr_color_storage_resource, gbuffer_resource, camera_resource);
                }
            
                auto extent = backend->get_swapchain_extent();
                ComputeWorkgroups workgroups = ComputeWorkgroups::from_extent(extent);
                ctx.compute(workgroups);
            },
            .type = RenderPassType::compute
        });
    
        add_pass({
            .name = "RTXGI_TEMPORAL_ACCUM",
            .reads = {
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI], RBImageUsageType::SampledFragment },
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_REPROJECTED], RBImageUsageType::SampledFragment },
            },
            .writes = {
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_ACCUM], RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
            },
            .execute = [this](RenderGraphContext& ctx)
            {
                if (ctx.bind_pipeline(rtx_gi_temporal_accum_pipeline))
                {
                    ctx.bind(hdr_color_output_resource, hdr_color_storage_resource, gbuffer_resource, camera_resource);
                }
                auto extent = backend->get_swapchain_extent();
                ComputeWorkgroups workgroups = ComputeWorkgroups::from_extent(extent);
                TemporalAccumPC pc;
                pc.reset = false;
                if (one_time_render_flags.contains("reset_temporal_accum"))
                {
                    if (one_time_render_flags["reset_temporal_accum"])
                        pc.reset = true;
                }
                ctx.push_constants(pc);
                ctx.compute(workgroups);
            },
            .type = RenderPassType::compute
        });
    
        add_pass({
            .name = "RTXGI_MOMENTS",
            .reads = {
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_ACCUM], RBImageUsageType::SampledFragment },
            },
            .writes = {
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_MOMENTS], RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
            },
            .execute = [this](RenderGraphContext& ctx)
            {
                if (ctx.bind_pipeline(rtx_gi_moments_pipeline))
                {
                    ctx.bind(hdr_color_output_resource, hdr_color_storage_resource, gbuffer_resource);
                }
                auto extent = backend->get_swapchain_extent();
                ComputeWorkgroups workgroups = ComputeWorkgroups::from_extent(extent);
                ctx.compute(workgroups);
            },
            .type = RenderPassType::compute
        });
    
        add_pass({
            .name = "RTXGI_SPATIAL",
            .reads = {
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_ACCUM], RBImageUsageType::SampledFragment },
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_MOMENTS], RBImageUsageType::SampledFragment },
    
                { gbuffer[GBUFFER_SLOTS::WORLD_NORMAL], RBImageUsageType::SampledFragment },
                { gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::SampledFragment },
            },
            .writes = {
                { hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_FILTERED], RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
            },
            .execute = [this](RenderGraphContext& ctx)
            {
                if (ctx.bind_pipeline(rtx_gi_spatial_filter_pipeline))
                {
                    ctx.bind(hdr_color_output_resource, hdr_color_storage_resource, gbuffer_resource);
                }
                auto extent = backend->get_swapchain_extent();
                ComputeWorkgroups workgroups = ComputeWorkgroups::from_extent(extent);
                ctx.compute(workgroups);
            },
            .type = RenderPassType::compute
        });
    
        if constexpr (render_settings::enable_nn_denoiser)
        {
            nn_denoiser::add_nn_denoiser_passes(nn_denoiser_state, *this, *renderer);
        }
    
        add_copy_pass("COPY_RTXGI_ACCUM_TO_HISTORY",
            hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_ACCUM],
            hdr_color_history[COLOR_OUTPUT_HDR::RTXGI_ACCUM]);
        
        add_copy_pass("COPY_moments_to_history", 
            hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_MOMENTS], hdr_color_history[COLOR_OUTPUT_HDR::RTXGI_MOMENTS]);
    }
    
    add_copy_pass("COPY_gbuffer_linear_depth_to_history", 
        gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], gbuffer_hist[GBUFFER_SLOTS::LINEAR_DEPTH]);
    
    // the history of the normals and positions is read by the ray traced GI only (SVGF, RTXGI reprojection):
    // the position copy alone is 0.3-0.5 ms at 2560x1506 (RGBA32F)
    if constexpr (render_settings::enable_raytracing)
    {
        add_copy_pass("COPY_gbuffer_world_normal_to_history",
            gbuffer[GBUFFER_SLOTS::WORLD_NORMAL], gbuffer_hist[GBUFFER_SLOTS::WORLD_NORMAL]);

        add_copy_pass("COPY_gbuffer_position_to_history",
            gbuffer[GBUFFER_SLOTS::POSITION], gbuffer_hist[GBUFFER_SLOTS::POSITION]);
    }

    // add_copy_pass("COPY_gbuffer_depth_to_history",
    //     gbuffer[GBUFFER_SLOT_DEPTH], gbuffer_hist[GBUFFER_SLOT_DEPTH]);
    
  
    
  
    add_pass({
        .name = Names::pass_geometry_translucent,
        .reads = {
             { brdf_lut, RBImageUsageType::SampledFragment, RBLoadOp::Load },    
        },
        .writes = { 
            { decal_albedo, RBImageUsageType::ColorAttachment, RBLoadOp::Clear },  
             { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::DepthStencilAttachment, RBLoadOp::Load },
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            PROFILE("Translucent");
                
            draw_scene(ctx);
        },
        .num_layers = num_pass_instances
    });
    
    
    // ---- ambient occlusion of the indirect light: GTAO, then two denoise passes ----
    if (gtao)
    {
        add_pass({
            .name = "GTAO",
            .condition = [] () { return cv_gtao_enabled.get(); },
            .reads = {
                { gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::Sampled },
            },
            .writes = {
                { gtao_raw, RBImageUsageType::StorageImage }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("GTAO");
                gtao->dispatch_gtao(ctx, camera_resource, gbuffer_resource, textures[gtao_raw.id].desc.extent);
            },
            .type = RenderPassType::compute
        });

        add_pass({
            .name = "GTAODenoise1",
            .condition = [] () { return cv_gtao_enabled.get(); },
            .reads = {
                { gtao_raw, RBImageUsageType::Sampled }
            },
            .writes = {
                { gtao_mid, RBImageUsageType::StorageImage }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("GTAODenoise1");
                gtao->dispatch_denoise(ctx, 0, textures[gtao_mid.id].desc.extent);
            },
            .type = RenderPassType::compute
        });

        add_pass({
            .name = "GTAODenoise2",
            .condition = [] () { return cv_gtao_enabled.get(); },
            .reads = {
                { gtao_mid, RBImageUsageType::Sampled }
            },
            .writes = {
                { gtao_result, RBImageUsageType::StorageImage }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("GTAODenoise2");
                gtao->dispatch_denoise(ctx, 1, textures[gtao_result.id].desc.extent);
            },
            .type = RenderPassType::compute
        });
    }
    
    add_pass({
        .name = Names::pass_lighting,
        .reads = {
            {  shadow_map, RBImageUsageType::SampledFragment },
            { cloud_shadow_map, RBImageUsageType::SampledFragment },
            { brdf_lut, RBImageUsageType::SampledFragment, RBLoadOp::Load },
            { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::NORMAL], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::WORLD_NORMAL], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::MOTION_VECTORS], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::ALBEDO_ROUGHNESS], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::POSITION], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::GEOMETRY_NORMAL], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::EMISSIVE], RBImageUsageType::SampledFragment },
            { decal_albedo, RBImageUsageType::SampledFragment },
            { gtao_result, RBImageUsageType::SampledFragment }
        },
        .writes = { 
            { hdr_color_present[COLOR_OUTPUT_HDR::BASE], RBImageUsageType::ColorAttachment, RBLoadOp::Clear },
            // image based specular is added by SSRComposite (SSR or reflection probes)
            { hdr_color_present[COLOR_OUTPUT_HDR::SPECULAR_WEIGHT], RBImageUsageType::ColorAttachment, RBLoadOp::Clear },
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            PROFILE("Geometry");
            
            if (ctx.bind_pipeline(lighting_pipeline))
            {
                ctx.bind(camera_resource, light_resource, hdr_color_output_resource, gbuffer_resource, dbuffer_resource, shadow_resource,
                    reflection_resource, gtao_resource);
            }
            
            // indirect light: SVGF-filtered RTXGI, or the reflection capture IBL without ray tracing
            ColorOutputConstants pc;
            pc.buffer_index = render_settings::enable_raytracing
                ? (uint32_t)COLOR_OUTPUT_HDR::RTXGI_FILTERED
                : LIGHTING_GI_FROM_IBL;
            pc.debug_flags = (uint32_t)ctx.params.get_int(LightingDebug::param, 0);
            pc.gtao_downscale = gtao && cv_gtao_enabled.get() ? gtao_downscale : 0;
            pc.gtao_foliage = std::clamp(cv_gtao_foliage.get(), 0.0f, 1.0f);
            if (!emissive_diagnostics_enabled())
                pc.debug_flags &= ~LightingDebug::emissive_watch;
            ctx.push_constants(pc);
            
            const bool emissive_watch = diag_enabled() && (pc.debug_flags & LightingDebug::emissive_watch) != 0;
            if (emissive_watch)
                backend->cmd_begin_query(ctx.cmd, diag_queries, ctx.frame * DIAG_COUNT + DIAG_LIGHTING);
            ctx.draw_fullscreen();
            if (emissive_watch)
                backend->cmd_end_query(ctx.cmd, diag_queries, ctx.frame * DIAG_COUNT + DIAG_LIGHTING);
        },
        .num_layers = num_pass_instances
    });
    
    // ---- sky behind the scene: clouds marched at a fraction of the resolution, then the sky over them ----
    if (sky)
    {
        add_pass({
            .name = "SkyMarch",
            .reads = {
                { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::Sampled },
                { clouds_history, RBImageUsageType::Sampled }
            },
            .writes = {
                { atmosphere_buffer, RBImageUsageType::StorageImage },
                { clouds_buffer, RBImageUsageType::StorageImage }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("SkyMarch");
                sky->dispatch_march(ctx, camera_resource, gbuffer_resource, textures[clouds_buffer.id].desc.extent);
            },
            .type = RenderPassType::compute
        });

        add_copy_pass("COPY_clouds_to_history", clouds_buffer, clouds_history);

        add_pass({
            .name = "Sky",
            .reads = {
                { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::SampledFragment },
                { atmosphere_buffer, RBImageUsageType::SampledFragment },
                { clouds_buffer, RBImageUsageType::SampledFragment }
            },
            .writes = {
                { hdr_color_present[COLOR_OUTPUT_HDR::BASE], RBImageUsageType::ColorAttachment, RBLoadOp::Load },
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("Sky");
                if (diag_enabled())
                    backend->cmd_begin_query(ctx.cmd, diag_queries, ctx.frame * DIAG_COUNT + DIAG_CLOUDS);
                sky->draw_sky(ctx, camera_resource, gbuffer_resource);
                if (diag_enabled())
                    backend->cmd_end_query(ctx.cmd, diag_queries, ctx.frame * DIAG_COUNT + DIAG_CLOUDS);
            },
        });
    }


    add_pass({
        .name = "SSR",
        .condition = [] () { return cv_ssr_enabled.get(); },
        .reads = {
            { hdr_color_present[COLOR_OUTPUT_HDR::BASE], RBImageUsageType::SampledFragment },
            { hdr_color_present[COLOR_OUTPUT_HDR::SPECULAR_WEIGHT], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::WORLD_NORMAL], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::ALBEDO_ROUGHNESS], RBImageUsageType::SampledFragment },
        },
        .writes = {
            { ssr_texture, RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            draw_ssr(ctx);
        },
    });
    
    
    
    add_pass({
        .name = "SSRComposite",
        .reads = {
            { hdr_color_present[COLOR_OUTPUT_HDR::BASE], RBImageUsageType::SampledFragment },
            { hdr_color_present[COLOR_OUTPUT_HDR::SPECULAR_WEIGHT], RBImageUsageType::SampledFragment },
            { ssr_texture, RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::WORLD_NORMAL], RBImageUsageType::SampledFragment },
            { gbuffer[GBUFFER_SLOTS::ALBEDO_ROUGHNESS], RBImageUsageType::SampledFragment }
        },
        .writes = {
            { hdr_color_present[COLOR_OUTPUT_HDR::INTERMEDIATE], RBImageUsageType::ColorAttachment, RBLoadOp::Load }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            draw_ssr_composite(ctx);
        },
    });
    
    add_pass({
        .name = "COPY_ssr_compose_to_hdr_base",
        .reads = {
            { hdr_color_present[COLOR_OUTPUT_HDR::INTERMEDIATE], RBImageUsageType::TransferSrc }
        },
        .writes = {
            { hdr_color_present[COLOR_OUTPUT_HDR::BASE], RBImageUsageType::TransferDst, RBLoadOp::Load }
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            CopyImageParams params;
            params.source = get_image(hdr_color_present[COLOR_OUTPUT_HDR::INTERMEDIATE]);
            params.dest = get_image(hdr_color_present[COLOR_OUTPUT_HDR::BASE]);
            ctx.copy_img(params);
        },
        .num_layers = 1,
        .type = RenderPassType::transfer
    });

    // ---- fog over the lit frame, reflections included: marched at a fraction of the resolution, lit through
    // the shadow maps (the light shafts), then blended over the scene and the sky ----
    if (sky)
    {
        const auto has_fog = [this] () { return sky->has_fog(); };

        add_pass({
            .name = "FogMarch",
            .condition = has_fog,
            .reads = {
                { gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::SampledFragment },
                { gbuffer_hist[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::SampledFragment },
                { shadow_map, RBImageUsageType::SampledFragment },
                { cloud_shadow_map, RBImageUsageType::SampledFragment },
                { fog_history, RBImageUsageType::SampledFragment }
            },
            .writes = {
                { fog_buffer, RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("FogMarch");
                sky->draw_fog_march(ctx, camera_resource, gbuffer_resource, light_resource, shadow_resource,
                    reflection_resource, textures[fog_buffer.id].desc.extent);
                ctx.backend.update_viewport(ctx.cmd, resolution, use_swapchain_extent);
            },
        });

        add_copy_pass("COPY_fog_to_history", fog_buffer, fog_history, true, has_fog);

        add_pass({
            .name = "Fog",
            .condition = has_fog,
            .reads = {
                { gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::SampledFragment },
                { fog_buffer, RBImageUsageType::SampledFragment }
            },
            .writes = {
                { hdr_color_present[COLOR_OUTPUT_HDR::BASE], RBImageUsageType::ColorAttachment, RBLoadOp::Load },
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("Fog");
                sky->draw_fog(ctx, gbuffer_resource);
            },
        });
    }

    // ---- particles over the lit frame and its fog: sprites tested against the depth of the scene ----
    if (particles)
    {
        add_pass({
            .name = "Particles",
            .condition = [this] () { return particles->has_particles(); },
            .reads = {
                { gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], RBImageUsageType::SampledFragment },
            },
            .writes = {
                { hdr_color_present[COLOR_OUTPUT_HDR::BASE], RBImageUsageType::ColorAttachment, RBLoadOp::Load },
                { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::DepthStencilAttachment, RBLoadOp::Load },
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                PROFILE("Particles");
                ctx.backend.update_viewport(ctx.cmd, resolution, use_swapchain_extent);
                particles->draw(ctx, camera_resource, light_resource, gbuffer_resource);
            },
        });
    }

    if (readback_nn && render_settings::enable_raytracing)
    {
        add_exr_dump_pass({
            .name   = "readback_nn_raw",
            .subdir = "nn",
            .entries = {
                {
                    .texture         = hdr_color_present[COLOR_OUTPUT_HDR::RTXGI],
                    .subdir = "noisy",
                    .out_channels    = 4,
                },
                {
                    .texture         = gbuffer[GBUFFER_SLOTS::MOTION_VECTORS],
                    .subdir = "mvec",
                    .out_channels    = 3,
                    .placeholder     = 0.0f,
                },
            },
            .condition = [this](const RenderGraphParameters& params) -> bool
            {
                return params.render_iter_id == 0 && get_render_flag("do_readback_nn");
            }
        });
        
        add_exr_dump_pass({
            .name   = "readback_nn",
            .subdir = "nn",
            .entries = {
                {
                    .texture         = hdr_color_present[COLOR_OUTPUT_HDR::RTXGI_ACCUM],
                    .subdir = "reference",
                    .out_channels    = 4,
                },
                {
                    .texture         = gbuffer[GBUFFER_SLOTS::WORLD_NORMAL],
                    .subdir = "normal",
                    .out_channels    = 4,
                },
                {
                    .texture         = gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH],
                    .subdir = "depth",
                    .out_channels    = 1,
                },
                {
                    .texture         = gbuffer[GBUFFER_SLOTS::EMISSIVE],
                    .subdir = "emissive",
                    .out_channels    = 4,
                },
                {
                    .texture         = gbuffer[GBUFFER_SLOTS::ALBEDO_ROUGHNESS],
                    .subdir = "albedo",
                    .out_channels    = 0,
                    .placeholder     = 0.0f,
                },
            },
            .condition = [this](const RenderGraphParameters& params) -> bool
            {
                return params.render_iter_id == params.num_runs - 1 && get_render_flag("do_readback_nn");
            }
        });
    }

}

void GenericRenderGraph::prepare_resources(RenderGraphContext& ctx)
{
    RenderGraph::prepare_resources(ctx);
   
    rebuild_camera_ubo(ctx);
    
    const int requested_view_mode = ctx.params.get_int(DebugViewParams::view_mode, 0);
    view_mode = requested_view_mode >= 0 && requested_view_mode < (int)DebugViewMode::COUNT
        ? (DebugViewMode)requested_view_mode
        : DebugViewMode::LIT;
    show_skeleton = ctx.params.get_int(DebugViewParams::show_skeleton, 0) != 0;
    
    prepared_batches.clear();

    if (draw_dump_frames > 0)
        --draw_dump_frames;
    if (auto dump = one_time_render_flags.find("dump_draws"); dump != one_time_render_flags.end() && dump->second)
    {
        draw_dump_frames = draw_dump_frame_count;
        const std::filesystem::path path = paths::get_cache_path() / "bench" / "draws.csv";
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << "frame,pass,cascade,mesh,lod,draws,triangles\n";
    }

    {
        PROFILE("DrawList::begin_frame");
        draw_list.begin_frame(ctx.frame);
    }

    emissive_watch_active = (ctx.params.get_int(LightingDebug::param, 0) & LightingDebug::emissive_watch) != 0;
    {
        PROFILE("read_diag_queries");
        read_diag_queries(ctx);
    }

    // -------- resources --------
    // this frame's copy of the primitive table (the previous frame may still be reading its own on the GPU)
    {
        PROFILE("upload_primitive_table");
        engine->scene_view->get_processor<SceneViewProcessor_Mesh>().upload_primitive_table(*primitive_table_resource, ctx.frame);
    }
    
    // the sky first: the light UBO carries its cloud shadow map, the probes capture it and take their
    // fallback ambient from it
    if (sky)
    {
        sky->prepare(ctx, *engine->scene_view, {
            .camera_position = current_camera_ubo.camera_pos,
            .time = RhGlobals::engine->world->get_time_seconds(),
            .atmosphere = get_image(atmosphere_buffer),
            .clouds = get_image(clouds_buffer),
            .clouds_history = get_image(clouds_history),
            .history_layer = history_index ^ 1,
            .downscale = sky_downscale,
            .fog = get_image(fog_buffer),
            .fog_history = get_image(fog_history),
            .fog_downscale = fog_downscale,
        });
    }

    prepare_geometry_resources(ctx);

    RenderPrimitive::lod_distance_scale() = std::clamp(cv_lod_scale.get(), 0.1f, 4.0f);

    // the base pass draws, before the passes: the occlusion culling passes run over them
    prepare_base_draws(ctx);
    if (occlusion)
    {
        // screen sized targets keep a zero extent in their description
        const Extent depth_extent = resolution.is_zero() ? backend->get_swapchain_extent() : resolution;
        occlusion->prepare(ctx, get_image(gbuffer[GBUFFER_SLOTS::DEPTH]), depth_extent);
    }

    if (foliage)
    {
        foliage->prepare(ctx, *engine->scene_view, {
            .view_proj = current_camera_ubo.proj * current_camera_ubo.view,
            .camera_position = current_camera_ubo.camera_pos,
            .hzb_valid = occlusion && cv_occlusion_culling.get(),
            .hzb_width = occlusion ? occlusion->get_hzb_width() : 0,
            .hzb_height = occlusion ? occlusion->get_hzb_height() : 0,
            .hzb_levels = occlusion ? occlusion->get_hzb_levels() : 0,
        });
    }

    if (particles)
    {
        particles->prepare(ctx, *engine->scene_view, {
            .camera_position = current_camera_ubo.camera_pos,
            .ambient = sky ? sky->get_sky_ambient() : glm::vec3(0.0f),
        });
    }

    if (reflection_probes)
    {
        reflection_probes->prepare(ctx, *engine->scene_view, {
            .camera_position = current_camera_ubo.camera_pos,
            .time = RhGlobals::engine->world->get_time_seconds(),
            .brdf_lut = get_image(brdf_lut),
            .sky_ambient = sky ? sky->get_sky_ambient() : glm::vec3(0.0f),
            .cloud_coverage = sky ? sky->get_cloud_coverage() : 0.0f,
        });
    }
    
    {
        PROFILE("prepare_wireframe / ssr / raytracing");
        prepare_wireframe_pass(ctx);
        prepare_ssr(ctx);
        prepare_raytracing(ctx);
    }

    gtao_resource->update_image("u_gtao", get_image(gtao_result), { .frame = ctx.frame });
    if (gtao)
    {
        gtao->prepare(ctx, {
            .downscale = gtao_downscale,
            .raw = get_image(gtao_raw),
            .mid = get_image(gtao_mid),
            .result = get_image(gtao_result),
        });
    }
    
    if constexpr (render_settings::enable_nn_denoiser)
    {
        nn_denoiser::prepare_resources(nn_denoiser_state, ctx);
    }
}

void GenericRenderGraph::prepare_raytracing(RenderGraphContext& ctx)
{
    auto& mesh_processor =
       engine->scene_view->get_processor<SceneViewProcessor_Mesh>();
    
    if (mesh_processor.is_dirty())
    {
        if constexpr (render_settings::enable_raytracing)
        {
            std::vector<TLASInfo> tlas_objs;

            tlas_objs.reserve(mesh_processor.primitives.size());

            for (auto& prim : mesh_processor.primitives)
            {
                // retired primitives (their mesh was unregistered) have no passes
                if (!prim.passes.empty() && !prim.passes.contains("GeometryTranslucent"))
                {
                    tlas_objs.push_back({
                        prim.mesh,
                        *prim.world,
                        prim.id,
                        prim.is_skinned() ? std::optional<uint32_t>(prim.skinned->instance_id) : std::nullopt
                    });
                }
            }

            tlas = backend->build_tlas(
                ctx.cmd,
                tlas_objs);
        }
        
        // the mesh table is used by raster passes too
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

void GenericRenderGraph::begin_frame()
{
    RenderGraph::begin_frame();

    // render.sky.downscale changed: the sky buffers get their new size
    const uint32_t downscale = uint32_t(std::clamp(cv_sky_downscale.get(), 1, 8));
    if (sky && downscale != sky_downscale)
    {
        sky_downscale = downscale;
        for (RGTextureHandle texture : { atmosphere_buffer, clouds_buffer, clouds_history })
            resize_texture(texture, downscale);
    }

    // the same for render.ssr.downscale
    const uint32_t ssr_scale = uint32_t(std::clamp(cv_ssr_downscale.get(), 1, 4));
    if (resolution.is_zero() && ssr_scale != ssr_downscale)
    {
        ssr_downscale = ssr_scale;
        resize_texture(ssr_texture, ssr_scale);
    }

    // the same for render.gtao.downscale
    const uint32_t gtao_scale = uint32_t(std::clamp(cv_gtao_downscale.get(), 1, 2));
    if (gtao && gtao_scale != gtao_downscale)
    {
        gtao_downscale = gtao_scale;
        for (RGTextureHandle texture : { gtao_raw, gtao_mid, gtao_result })
            resize_texture(texture, gtao_scale);
    }

    // the same for render.fog.downscale
    const uint32_t fog_scale = uint32_t(std::clamp(cv_fog_downscale.get(), 1, 8));
    if (sky && fog_scale != fog_downscale)
    {
        fog_downscale = fog_scale;
        for (RGTextureHandle texture : { fog_buffer, fog_history })
            resize_texture(texture, fog_scale);
    }
}

void GenericRenderGraph::end_frame()
{
    RenderGraph::end_frame();
    
    history_index ^= 1;
}

void GenericRenderGraph::rebuild_camera_ubo(RenderGraphContext& ctx)
{
    if (ctx.params.render_iter_id == 0)
    {
        const auto prev_proj = current_camera_ubo.proj;
        const auto prev_view = current_camera_ubo.view;
        
        current_camera_ubo = make_camera_ubo(ctx, false, 0);
        current_camera_ubo.prev_proj = prev_proj;
        current_camera_ubo.prev_view = prev_view;
    }
    else
    {
        current_camera_ubo.prev_proj = current_camera_ubo.proj;
        current_camera_ubo.prev_view = current_camera_ubo.view;
    }
}

void GenericRenderGraph::on_pso_built()
{
    if constexpr (render_settings::enable_raytracing)
    {
        rtx_gi_spatial_filter_pipeline = rtx_gi_spatial_filter_pipeline_family->request_pipeline({});
        rtx_gi_moments_pipeline = rtx_gi_moments_pipeline_family->request_pipeline({});
        rtx_gi_temporal_accum_pipeline = rtx_gi_temporal_accum_pipeline_family->request_pipeline({});
        rtx_gi_reproject_pipeline = rtx_gi_reproject_pipeline_family->request_pipeline({});
        rtx_gi_pipeline = rtx_gi_pipeline_family->request_pipeline({});
    }
    ssr_composite_pipeline = ssr_composite_pipeline_family->request_pipeline({});   
    ssr_pipeline = ssr_pipeline_family->request_pipeline({});
    shadow_debug_pipeline = shadow_debug_pipeline_family->request_pipeline({});
    wireframe_pipeline = request_pipeline(wireframe_pipeline_family, {});
    wireframe_mesh_pipeline = request_pipeline(wireframe_mesh_pipeline_family, {});
    skeleton_pipeline = request_pipeline(skeleton_pipeline_family, {});
    debug_lines_pipeline = request_pipeline(debug_lines_pipeline_family, {});
    tonemap_pipeline = request_pipeline(tonemap_pipeline_family, {});
    lighting_pipeline = request_pipeline(lighting_pipeline_family, {});
    skinning_pipeline = skinning_pipeline_family->request_pipeline({});
    nn_denoiser::on_pso_built(nn_denoiser_state);
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

        write_shadow_cascades(light_ubo.dir_light);

        if (sky)
        {
            const SkyRenderer::CloudShadow& cloud_shadow = sky->get_cloud_shadow();
            light_ubo.dir_light.cloud_shadow_u = cloud_shadow.u;
            light_ubo.dir_light.cloud_shadow_v = cloud_shadow.v;
            light_ubo.dir_light.cloud_shadow_params = cloud_shadow.params;
        }
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
            // reflection probes: ReflectionProbeSystem::prepare (after this function)

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
            
            shadow_resource->update_image(
                "u_shadow_depth_debug",
                get_image(shadow_map),
            {
                    .frame = ctx.frame
                });

            shadow_resource->update_image(
                "u_cloud_shadow",
                get_image(cloud_shadow_map),
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
        "u_hdr_color_history",
        get_image(hdr_color_present[COLOR_OUTPUT_HDR::BASE]),
        {.frame=ctx.frame});

    ssr_resource->update_image(
        "u_ssr",
        get_image(ssr_texture),
        {.frame=ctx.frame});
    
}

void GenericRenderGraph::setup_hdr_color_table(RenderGraphContext& ctx) const
{
    for (uint16_t index = 0; index < hdr_color_present.size(); ++index)
    {
        hdr_color_output_resource->update_image(
            "u_hdr_color_present",
            get_image(hdr_color_present[index]),
            {
                .frame=ctx.frame,
                .array_index = index
            });
        hdr_color_storage_resource->update_image(
            "u_hdr_color_present_storage",
            get_image(hdr_color_present[index]),
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


std::array<ShadowCascade, shadow_cascade_count> GenericRenderGraph::fit_shadow_cascades(const glm::vec3& light_direction) const
{
    std::array<ShadowCascade, shadow_cascade_count> result;
    const CameraUBO& camera = current_camera_ubo;
    const float near = std::max(camera.near, 0.05f);
    const float far = std::max(std::min(camera.far, Constants::shadow_distance), near + 1.0f);
    const uint32_t tile_texels = Constants::shadowmap_extent.width / Constants::shadow_atlas_tiles;

    // splits between uniform and logarithmic (practical split scheme)
    std::array<float, shadow_cascade_count + 1> splits{};
    splits[0] = near;
    for (uint32_t i = 1; i <= shadow_cascade_count; ++i)
    {
        const float t = float(i) / float(shadow_cascade_count);
        const float logarithmic = near * std::pow(far / near, t);
        const float uniform = near + (far - near) * t;
        splits[i] = glm::mix(uniform, logarithmic, Constants::shadow_split_lambda);
    }

    // light space: rotation only, the cascades are fitted in it
    const glm::vec3 up = std::abs(light_direction.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 light_view = glm::lookAt(glm::vec3(0.0f), light_direction, up);

    // casters anywhere in the scene reach every cascade: its depth range covers the scene bounds
    const AABB& scene_bounds = engine->scene_view->world_aabb;
    glm::vec3 scene_corners[8];
    scene_bounds.get_corners(scene_corners);
    float scene_min_z = std::numeric_limits<float>::max();
    float scene_max_z = std::numeric_limits<float>::lowest();
    for (const glm::vec3& corner : scene_corners)
    {
        const float z = (light_view * glm::vec4(corner, 1.0f)).z;
        scene_min_z = std::min(scene_min_z, z);
        scene_max_z = std::max(scene_max_z, z);
    }

    const glm::vec3 position(camera.inv_view[3]);
    const glm::vec3 right(camera.inv_view[0]);
    const glm::vec3 camera_up(camera.inv_view[1]);
    const glm::vec3 forward = -glm::vec3(camera.inv_view[2]);
    const float tan_x = 1.0f / std::abs(camera.proj[0][0]);
    const float tan_y = 1.0f / std::abs(camera.proj[1][1]);

    for (uint32_t cascade = 0; cascade < shadow_cascade_count; ++cascade)
    {
        // bounding sphere of the frustum slice: the size does not change when the camera turns (no shimmering)
        std::array<glm::vec3, 8> corners;
        glm::vec3 center(0.0f);
        for (uint32_t i = 0; i < 8; ++i)
        {
            const float depth = splits[cascade + (i >> 2)];
            const float sx = (i & 1) ? 1.0f : -1.0f;
            const float sy = (i & 2) ? 1.0f : -1.0f;
            corners[i] = position + forward * depth + right * (sx * tan_x * depth) + camera_up * (sy * tan_y * depth);
            center += corners[i] / 8.0f;
        }
        float radius = 0.0f;
        for (const glm::vec3& corner : corners)
            radius = std::max(radius, glm::length(corner - center));
        radius = std::ceil(radius * 4.0f) / 4.0f;

        // the center moves in whole texels (no shimmering when the camera moves)
        const float texel = 2.0f * radius / float(tile_texels);
        glm::vec3 center_ls(light_view * glm::vec4(center, 1.0f));
        center_ls.x = std::floor(center_ls.x / texel) * texel;
        center_ls.y = std::floor(center_ls.y / texel) * texel;

        // the light looks along -z: near / far are distances along it
        const float z_near = -std::max(scene_max_z, center_ls.z + radius) - 1.0f;
        const float z_far = -std::min(scene_min_z, center_ls.z - radius) + 1.0f;
        const glm::mat4 projection = glm::orthoZO(center_ls.x - radius, center_ls.x + radius,
                                                  center_ls.y - radius, center_ls.y + radius, z_near, z_far);

        result[cascade] = ShadowCascade{
            .vp = projection * light_view,
            .center = center,
            .radius = radius,
            .texel = texel,
            .depth_range = z_far - z_near,
            .split_near = splits[cascade],
            .light_direction = light_direction,
            .valid = true,
        };
    }
    return result;
}

void GenericRenderGraph::update_shadow_cascades(const glm::vec3& light_direction)
{
    // a new atlas (first frame, graph rebuilt) holds no cascade yet
    const RBImageHandle atlas = get_image(shadow_map);
    if (atlas != shadow_atlas_image)
    {
        shadow_atlas_image = atlas;
        for (ShadowCascade& cascade : shadow_cascades)
            cascade.valid = false;
    }

    // the far cascades take turns: 2 on odd frames, 3 on even ones (with the default intervals 2 and 4)
    const uint64_t frame = shadow_frame++;
    const uint64_t interval_2 = uint64_t(std::max(cv_shadow_interval_2.get(), 1));
    const uint64_t interval_3 = uint64_t(std::max(cv_shadow_interval_3.get(), 1));
    const std::array<bool, shadow_cascade_count> scheduled{
        true,
        true,
        frame % interval_2 == 1 % interval_2,
        frame % interval_3 == 0,
    };

    const std::array<ShadowCascade, shadow_cascade_count> fitted = fit_shadow_cascades(light_direction);
    for (uint32_t i = 0; i < shadow_cascade_count; ++i)
    {
        ShadowCascade& cascade = shadow_cascades[i];
        const ShadowCascade& fit = fitted[i];
        // a stale tile still shades what it covers; it has to follow when the camera left it (teleport, fast
        // flight), the field of view changed or the sun turned (its shadows would visibly lag)
        const bool moved = glm::length(fit.center - cascade.center) > 0.25f * fit.radius
            || std::abs(fit.radius - cascade.radius) > 0.02f * fit.radius;
        const bool sun_turned = glm::dot(fit.light_direction, cascade.light_direction) < std::cos(glm::radians(1.0f));
        const bool render = !cascade.valid || scheduled[i] || moved || sun_turned;
        if (render)
            cascade = fit;
        cascade.rendered_this_frame = render;
    }
}

void GenericRenderGraph::write_shadow_cascades(DirectionalLight& light) const
{
    const bool all_valid = std::ranges::all_of(shadow_cascades, [] (const ShadowCascade& c) { return c.valid; });
    for (uint32_t i = 0; i < shadow_cascade_count; ++i)
    {
        light.cascade_vp[i] = shadow_cascades[i].vp;
        light.cascade_texel[i] = shadow_cascades[i].texel;
        light.cascade_depth_range[i] = shadow_cascades[i].depth_range;
    }
    // before the first shadow pass nothing is in the atlas: lit
    light.cascade_params = glm::vec4(all_valid ? float(shadow_cascade_count) : 0.0f, 0.1f,
        1.0f / float(Constants::shadowmap_extent.width), float(Constants::shadow_atlas_tiles));
}

void GenericRenderGraph::dispatch_skinning(RenderGraphContext& ctx)
{
    PROFILE("GenericRenderGraph::dispatch_skinning");
    
    auto& mesh_processor = engine->scene_view->get_processor<SceneViewProcessor_Mesh>();
    
    std::vector<RenderPrimitive*> pending;
    for (auto& prim : mesh_processor.primitives)
    {
        // no pose: retired primitive (its mesh was unregistered)
        if (prim.is_skinned() && prim.skinning && prim.skinned_pose_version != prim.skinning->version)
            pending.push_back(&prim);
    }
    
    if (pending.empty())
        return;
    
    backend->cmd_skinning_begin_barrier(ctx.cmd);
    
    ctx.bind_pipeline(skinning_pipeline);
    
    std::vector<uint32_t> refit_instances;
    refit_instances.reserve(pending.size());
    
    for (RenderPrimitive* prim : pending)
    {
        const SkinnedMeshGPU& gpu = *prim->skinned;
        
        const RBDeviceAddress bones = backend->upload_bone_matrices(
            gpu.instance_id, ctx.frame, prim->skinning->skinning_matrices, prim->skinning->morph_weights);
        
        SkinningPushConstants pc{};
        pc.src_vertices = gpu.src_vertex_address;
        pc.skin = gpu.skin_address;
        pc.bones = bones.handle;
        pc.dst_vertices = gpu.dst_vertex_address;
        pc.vertex_count = gpu.vertex_count;
        pc.bone_count = gpu.bone_count;
        pc.morph_offsets = gpu.morph_offsets_address;
        pc.morph_deltas = gpu.morph_deltas_address;
        // weights follow the bone matrices in the same ring slot
        pc.morph_weights = gpu.morph_count > 0 ? bones.handle + sizeof(glm::mat4) * gpu.bone_count : 0;
        pc.morph_count = gpu.morph_count;
        ctx.push_constants(pc);
        
        ctx.compute({ (gpu.vertex_count + SKINNING_GROUP_SIZE - 1) / SKINNING_GROUP_SIZE, 1, 1 });
        
        prim->skinned_pose_version = prim->skinning->version;
        refit_instances.push_back(gpu.instance_id);
    }
    
    backend->cmd_skinning_end_barrier(ctx.cmd);
    
    if constexpr (render_settings::enable_raytracing)
    {
        backend->cmd_refit_skinned_blas(ctx.cmd, refit_instances);
        
        // BLAS bounds changed -> TLAS is rebuilt on the next prepare_raytracing
        mesh_processor.set_dirty(true);
    }
}

namespace
{
    // cv_debug_hide_base / cv_debug_hide_shadow: "part,part,-kept part", '*' hides every mesh
    struct MeshNameFilter
    {
        explicit MeshNameFilter(const std::string& text)
        {
            for (const auto part : std::views::split(text, ','))
            {
                const std::string_view term(part.begin(), part.end());
                if (term.empty())
                    continue;
                if (term == "*")
                    hide_all = true;
                else if (term.front() == '-')
                    keep.emplace_back(term.substr(1));
                else
                    hide.emplace_back(term);
            }
        }

        bool hides(const RenderPrimitive& prim) const
        {
            if (!hide_all && hide.empty())
                return false;
            const std::string& name = prim.mesh.mesh.get().name;
            const auto in_name = [&] (const std::string& term) { return name.find(term) != std::string::npos; };
            return (hide_all || std::ranges::any_of(hide, in_name)) && !std::ranges::any_of(keep, in_name);
        }

        std::vector<std::string> hide;
        std::vector<std::string> keep;
        bool hide_all = false;
    };
}

void GenericRenderGraph::draw_scene(RenderGraphContext& ctx)
{
    PROFILE("GenericRenderGraph::draw_scene");

    ctx.backend.update_viewport(
        ctx.cmd,
        resolution,
        use_swapchain_extent);

    const bool translucent = ctx.pass_name == Names::pass_geometry_translucent;
    const uint32_t geometry_debug = (uint32_t)ctx.params.get_int(GeometryDebug::param, 0);
    const auto bind_resources = [&] (const RenderPrimitivePassInfo&)
    {
        ctx.bind(camera_resource, mesh_table_resource,
                 shadow_resource, light_resource, reflection_resource, pbr_material_table_resource, textures_resource,
                 primitive_table_resource);
        if (translucent)
            ctx.bind(hdr_color_output_resource);
    };

    // the base pass of the main graph was put into the draw list before the passes (prepare_base_draws): the
    // occlusion culling decides which of its draws run in the early and in the late pass
    const bool late = ctx.pass_name == Names::pass_geometry_base_late;
    if (late || (ctx.pass_name == Names::pass_geometry_base && base_draws_prepared))
    {
        const bool culled = occlusion && cv_occlusion_culling.get();
        if (late && !culled)
            return;
        const bool diag_geometry = diag_enabled() && !late;
        if (diag_geometry)
            backend->cmd_begin_query(ctx.cmd, diag_queries, ctx.frame * DIAG_COUNT + DIAG_GEOMETRY);
        if (culled)
            draw_groups(ctx, base_groups, geometry_debug, bind_resources, occlusion->get_commands_buffer(),
                late ? occlusion->get_late_offset() : 0);
        else
            draw_groups(ctx, base_groups, geometry_debug, bind_resources);
        if (diag_geometry)
            backend->cmd_end_query(ctx.cmd, diag_queries, ctx.frame * DIAG_COUNT + DIAG_GEOMETRY);
        return;
    }

    std::vector<ViewRenderItem> items;
    auto view_info = build_view_info(ctx, false);
    engine->scene_view->get_processor<SceneViewProcessor_Mesh>().gather_for_view(
        view_info.view,
        view_info.frustum,
        ctx.pass_name,
        items);

    PROFILE("GenericRenderGraph::draw_scene - items");
    const bool base_pass = ctx.pass_name == Names::pass_geometry_base;

    std::vector<const RenderPrimitive*> primitives;
    primitives.reserve(items.size());
    const MeshNameFilter hidden(base_pass ? cv_debug_hide_base.get() : std::string());
    for (const ViewRenderItem& item : items)
        if (!hidden.hides(*item.primitive))
            primitives.push_back(item.primitive);

    const bool diag_geometry = diag_enabled() && base_pass;
    if (diag_geometry)
        backend->cmd_begin_query(ctx.cmd, diag_queries, ctx.frame * DIAG_COUNT + DIAG_GEOMETRY);

    // translucent draws blend: they keep their order across pipelines
    draw_items(ctx, own_meshes(primitives), geometry_debug, translucent, bind_resources);

    if (diag_geometry)
        backend->cmd_end_query(ctx.cmd, diag_queries, ctx.frame * DIAG_COUNT + DIAG_GEOMETRY);
}

void GenericRenderGraph::prepare_base_draws(RenderGraphContext& ctx)
{
    PROFILE("GenericRenderGraph::prepare_base_draws");

    base_groups.clear();
    base_first_command = draw_list.command_count();
    base_command_count = 0;
    base_draws_prepared = false;
    // the main graph only (one view, one pass instance)
    if (num_pass_instances != 1)
        return;

    const glm::mat4 view = current_camera_ubo.view;
    const Frustum frustum = Frustum::from_view_projection(current_camera_ubo.proj * view);
    std::vector<ViewRenderItem> items;
    engine->scene_view->get_processor<SceneViewProcessor_Mesh>().gather_for_view(view, frustum,
        Names::pass_geometry_base, items);

    std::vector<const RenderPrimitive*> primitives;
    primitives.reserve(items.size());
    const MeshNameFilter hidden(cv_debug_hide_base.get());
    for (const ViewRenderItem& item : items)
        if (!hidden.hides(*item.primitive))
            primitives.push_back(item.primitive);

    const std::vector<MeshDraw> draws = own_meshes(primitives);
    if (draw_dump_frames > 0)
        dump_draws(Names::pass_geometry_base, 0, draws);
    base_groups = record_groups(Names::pass_geometry_base, draws, /* with_bounds = */ true);
    base_command_count = draw_list.command_count() - base_first_command;
    base_draws_prepared = true;

    // the copies of the culling hold a limited number of commands: beyond them, no culling this frame
    if (occlusion && base_first_command + base_command_count > occlusion->get_capacity())
    {
        LogGenericRG.Log("Occlusion culling: %u base pass draws exceed the %u of the culled command copies",
            base_first_command + base_command_count, occlusion->get_capacity());
        cv_occlusion_culling.set(false);
    }
}

// Frame counters of a pass (prof::count, read by the benchmark): its draws and triangles
static void count_draws(Name pass, uint64_t draws, uint64_t indices)
{
    static std::unordered_map<Name, std::pair<std::string, std::string>> names;
    auto it = names.find(pass);
    if (it == names.end())
        it = names.emplace(pass, std::pair{ "draws " + pass.to_string(), "triangles " + pass.to_string() }).first;
    prof::count(it->second.first, draws);
    prof::count(it->second.second, indices / 3);
}

void GenericRenderGraph::dump_draws(Name pass, uint32_t debug_id, std::span<const MeshDraw> draws) const
{
    struct Row
    {
        uint64_t draws = 0;
        uint64_t triangles = 0;
    };
    // mesh asset, LOD level
    std::map<std::pair<std::string, uint32_t>, Row> rows;
    for (const MeshDraw& draw : draws)
    {
        if (!draw.primitive->get_pass_info(pass))
            continue;
        Row& row = rows[{ draw.primitive->mesh.mesh.get().name, draw.primitive->lod_level }];
        ++row.draws;
        row.triangles += draw.indices.index_count / 3;
    }

    std::ofstream file(paths::get_cache_path() / "bench" / "draws.csv", std::ios::app);
    // debug_id: the cascade of a shadow pass
    for (const auto& [key, row] : rows)
        file << std::format("{},{},{},{},{},{},{}\n", draw_dump_frame_count - draw_dump_frames, pass.to_string(), debug_id,
            key.first, key.second, row.draws, row.triangles);
}

std::vector<GenericRenderGraph::MeshDraw> GenericRenderGraph::own_meshes(const std::vector<const RenderPrimitive*>& primitives)
{
    std::vector<MeshDraw> draws;
    draws.reserve(primitives.size());
    for (const RenderPrimitive* prim : primitives)
        draws.push_back({ prim, (uint32_t)prim->mesh_index, prim->indices });
    return draws;
}

std::vector<GenericRenderGraph::DrawGroup> GenericRenderGraph::record_groups(Name pass, std::span<const MeshDraw> items,
    bool with_bounds)
{
    PROFILE("GenericRenderGraph::record_groups");

    // group by pipeline and index block (a handful per pass), the order of the items is kept inside a group
    std::vector<std::pair<DrawGroup, std::vector<std::pair<const MeshDraw*, const RenderPrimitivePassInfo*>>>> groups;
    for (const MeshDraw& draw : items)
    {
        const RenderPrimitivePassInfo* info = draw.primitive->get_pass_info(pass);
        if (!info)
            continue;
        auto group = std::ranges::find_if(groups, [&] (const auto& g)
        {
            return g.first.info->pipeline == info->pipeline && g.first.index_block == draw.indices.block;
        });
        if (group == groups.end())
        {
            groups.push_back({ DrawGroup{ .info = info, .index_block = draw.indices.block }, {} });
            group = std::prev(groups.end());
        }
        group->second.push_back({ &draw, info });
    }

    uint64_t index_count = 0;
    uint64_t draw_count = 0;
    std::vector<DrawGroup> result;
    result.reserve(groups.size());
    for (auto& [group, draws] : groups)
    {
        group.first_command = draw_list.command_count();
        for (const auto& [draw, info] : draws)
        {
            draw_list.add_draw({
                .mesh_id = draw->mesh_index,
                .primitive_id = draw->primitive->id,
                .material_id = info->material_index,
            }, draw->indices, with_bounds ? &draw->primitive->bounds : nullptr);
            index_count += draw->indices.index_count;
        }
        group.command_count = draw_list.command_count() - group.first_command;
        draw_count += group.command_count;
        result.push_back(group);
    }
    count_draws(pass, draw_count, index_count);
    return result;
}

void GenericRenderGraph::draw_groups(RenderGraphContext& ctx, std::span<const DrawGroup> groups, uint32_t debug_id,
    const std::function<void(const RenderPrimitivePassInfo&)>& bind_resources, std::optional<RBBufferHandle> commands,
    uint32_t command_offset)
{
    for (const DrawGroup& group : groups)
    {
        if (ctx.bind_pipeline(group.info->pipeline))
        {
            bind_resources(*group.info);
            draw_list.bind(ctx);
            ctx.push_constants(ModelPushConstants{ .debug_id = debug_id });
        }
        draw_list.draw_indirect(ctx, group.index_block, group.first_command, group.command_count, commands, command_offset);
    }
}

void GenericRenderGraph::draw_items(RenderGraphContext& ctx, std::span<const MeshDraw> items,
    uint32_t debug_id, bool in_order, const std::function<void(const RenderPrimitivePassInfo&)>& bind_resources)
{
    PROFILE("GenericRenderGraph::draw_items");

    if (draw_dump_frames > 0)
        dump_draws(ctx.pass_name, debug_id, items);

    if (in_order)
    {
        uint64_t draw_count = 0;
        uint64_t index_count = 0;
        for (const MeshDraw& draw : items)
        {
            const RenderPrimitivePassInfo* info = draw.primitive->get_pass_info(ctx.pass_name);
            if (!info)
                continue;
            if (ctx.bind_pipeline(info->pipeline))
            {
                bind_resources(*info);
                draw_list.bind(ctx);
                ctx.push_constants(ModelPushConstants{ .debug_id = debug_id });
            }
            const uint32_t first_command = draw_list.command_count();
            draw_list.add_draw({ .mesh_id = draw.mesh_index, .primitive_id = draw.primitive->id,
                .material_id = info->material_index }, draw.indices);
            draw_list.draw_indirect(ctx, draw.indices.block, first_command, 1);
            ++draw_count;
            index_count += draw.indices.index_count;
        }
        count_draws(ctx.pass_name, draw_count, index_count);
        return;
    }

    draw_groups(ctx, record_groups(ctx.pass_name, items, false), debug_id, bind_resources);
}

bool GenericRenderGraph::emissive_diagnostics_enabled()
{
    static const bool enabled = []
    {
        const char* watch = std::getenv("RHEA_EMISSIVE_WATCH");
        return (watch && watch[0] == '1') || std::getenv("RHEA_SOAK_SECONDS") != nullptr;
    }();
    return enabled;
}

void GenericRenderGraph::read_diag_queries(RenderGraphContext& ctx)
{
    // without the pool diag_enabled() stays false: no queries are recorded anywhere
    if (num_pass_instances != 1 || !emissive_diagnostics_enabled())
        return;
    if (diag_queries.handle == 0)
        diag_queries = backend->create_occlusion_pool(kRenderMaxFramesInFlight * DIAG_COUNT);
    if (diag_queries.handle == 0)
        return;

    // this frame slot's fence was waited: its previous queries are complete (or were never written)
    const uint32_t first = ctx.frame * DIAG_COUNT;
    ++diag_frame_counter;
    uint64_t clouds = 0;
    uint64_t geometry = 0;
    uint64_t lighting = 0;
    const bool has_clouds = backend->read_query_results(diag_queries, first + DIAG_CLOUDS, 1, &clouds);
    const bool has_geometry = backend->read_query_results(diag_queries, first + DIAG_GEOMETRY, 1, &geometry);
    const bool has_lighting = backend->read_query_results(diag_queries, first + DIAG_LIGHTING, 1, &lighting);
    backend->reset_queries(diag_queries, first, DIAG_COUNT);

    const Extent extent = backend->get_swapchain_extent();
    const double pixels = double(extent.width) * double(extent.height);

    // the frame recorded now is inside a RenderDoc capture: its watch result arrives MAX_FRAMES_IN_FLIGHT frames later
    if (platform::renderdoc::is_frame_capturing())
        LogGenericRG.Log("Emissive watch: RenderDoc is capturing frame %llu (its result is reported at frame %llu)",
            (unsigned long long)diag_frame_counter, (unsigned long long)diag_frame_counter + kRenderMaxFramesInFlight);

    // emissive watch: the lighting pass discarded every legacy-model pixel with a non-zero emissive
    // (these frames were recorded MAX_FRAMES_IN_FLIGHT frames ago)
    if (has_lighting && pixels > 0.0)
    {
        const double bad = std::max(0.0, pixels - double(lighting));
        const double now = RhGlobals::engine->world->get_time_seconds();
        // the checked periphery (see lighting.frag) is 58% of the screen
        const bool flagged = bad > pixels * 0.01;
        ++emissive_watch_frames;
        emissive_watch_flagged += flagged ? 1 : 0;
        emissive_watch_bursts += flagged && !emissive_watch_prev_flagged ? 1 : 0;
        emissive_watch_prev_flagged = flagged;
        if (emissive_watch_last_summary_time < 0.0)
            emissive_watch_last_summary_time = now;
        if (now - emissive_watch_last_summary_time >= 30.0)
        {
            LogGenericRG.Log("Emissive watch summary (t=%.0f s, %ux%u): %llu frames, %llu flagged, %llu bursts",
                now, extent.width, extent.height,
                (unsigned long long)emissive_watch_frames, (unsigned long long)emissive_watch_flagged,
                (unsigned long long)emissive_watch_bursts);
            emissive_watch_last_summary_time = now;
        }
        if (flagged)
        {
            ++emissive_watch_frames_since_log;
            if (now - emissive_watch_last_log_time > 0.5)
            {
                // wall clock: to line up with external logs (nvidia-smi clocks / power states)
                const long long wall_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                LogGenericRG.Log("Emissive watch (frame %llu, t=%.2f s, unix_ms %lld): %.0f corrupted pixels (%.1f%% of the screen), %u flagged frames since the last message",
                    (unsigned long long)diag_frame_counter, now, wall_ms, bad, 100.0 * bad / pixels, emissive_watch_frames_since_log);
                emissive_watch_last_log_time = now;
                emissive_watch_frames_since_log = 0;
            }
            // RenderDoc (RHEA_RENDERDOC=1): capture the next frames of the flash, to replay them later
            // (a clean replay of a frame that was corrupted live = the commands are right)
            if (platform::renderdoc::available() && renderdoc_captures_requested < 3 &&
                now - renderdoc_last_request_time > 2.0)
            {
                ++renderdoc_captures_requested;
                renderdoc_last_request_time = now;
                platform::renderdoc::trigger_capture(3);
                LogGenericRG.Log("Emissive watch: RenderDoc capture of the next 3 frames requested at frame %llu (request %u/3, captures so far %u)",
                    (unsigned long long)diag_frame_counter, renderdoc_captures_requested, platform::renderdoc::get_num_captures());
            }
            // the flash usually lasts several frames: this frame is likely still corrupted
            const uint32_t max_dumps = (uint32_t)std::max(0, ctx.params.get_int("emissive_watch_dumps", 5));
            if (emissive_watch_dumps < max_dumps && now - emissive_watch_last_dump_time > 1.0)
            {
                ++emissive_watch_dumps;
                emissive_watch_last_dump_time = now;
                set_flag("debug_dump_frame", true, false, true);
                LogGenericRG.Log("Emissive watch: dumping g-buffer of frame %llu to cache/debug_dump (%u/%u), the corrupted frame is g_emissive_ring layer %u",
                    (unsigned long long)diag_frame_counter, emissive_watch_dumps, max_dumps, emissive_ring_layer_of_slot[ctx.frame]);
            }
        }
    }

    if (!has_clouds)
        return;
    if (diag_frame_counter == 300)
        LogGenericRG.Log("Sky flash diagnostics active: frame 300 sky %.1f%% of the screen, base pass samples %s",
            pixels > 0.0 ? 100.0 * double(clouds) / pixels : 0.0,
            has_geometry ? std::to_string(geometry).c_str() : "n/a");
    if (pixels > 0.0 && double(clouds) > pixels * 0.9)
    {
        LogGenericRG.Log("Sky flash (frame %llu, t=%.2f s): the sky covered %.0f%% of the screen, base pass samples: %s",
            (unsigned long long)diag_frame_counter, RhGlobals::engine->world->get_time_seconds(),
            100.0 * double(clouds) / pixels,
            has_geometry ? std::to_string(geometry).c_str() : "n/a");
    }
}


void GenericRenderGraph::draw_scene_shadow(RenderGraphContext& ctx)
{
    PROFILE("draw_scene_shadow");
    
    auto& mesh_processor = engine->scene_view->get_processor<SceneViewProcessor_Mesh>();

    const auto dir_light = engine->scene_view->get_processor<SceneViewProcessor_Light>().get_directional_light();
    if (!dir_light)
        return;

    // time sliced: the atlas keeps the tiles of the cascades not rendered this frame (the pass loads it), the
    // light UBO carries the matrices every tile was rendered with. Uploaded now: every pass of the frame (and
    // the shadow shaders) must see this frame's cascades, even when nothing binds a shadow pipeline.
    update_shadow_cascades(glm::normalize(dir_light->direction));
    light_resource->update_uniform_buffer("light_ubo", build_light_ubo(current_camera_ubo.camera_pos), ctx.frame);

    // one tile of the atlas per cascade, the cascade index goes to the shaders in the push constants
    const uint32_t tile = Constants::shadowmap_extent.width / Constants::shadow_atlas_tiles;
    std::vector<MeshDraw> draws;
    draws.reserve(mesh_processor.primitives.size());
    const MeshNameFilter hidden(cv_debug_hide_shadow.get());
    for (uint32_t cascade = 0; cascade < shadow_cascade_count; ++cascade)
    {
        if (!shadow_cascades[cascade].rendered_this_frame)
            continue;

        const Frustum frustum = Frustum::from_view_projection(shadow_cascades[cascade].vp);
        const float texel = shadow_cascades[cascade].texel;
        const float proxy_error = texel * cv_shadow_proxy_texels.get();
        // casters smaller than this leave a speck of a texel or two: not drawn
        const float min_caster_size = texel * cv_shadow_min_caster_texels.get();
        const float lod_min_distance = cv_shadow_cascade_lods.get() ? shadow_cascades[cascade].split_near : 0.0f;

        draws.clear();
        // the LOD follows the camera, not the light: the shadow of what the camera sees
        const glm::vec3 viewer = glm::vec3(current_camera_ubo.camera_pos);
        for (const RenderPrimitive& prim : mesh_processor.primitives)
        {
            // the cascade shades what is at least split_near away: casters in the LOD of that distance
            if (!prim.in_shadow_lod_range(viewer, lod_min_distance) || !frustum.test_aabb_world(prim.bounds) || hidden.hides(prim))
                continue;
            if (glm::length(prim.bounds.max - prim.bounds.min) < min_caster_size)
                continue;

            MeshDraw draw{ &prim, (uint32_t)prim.mesh_index, prim.indices };
            // the coarsest simplified caster within the error
            const RenderPrimitive::ShadowProxyDraw* proxy = nullptr;
            for (const RenderPrimitive::ShadowProxyDraw& candidate : prim.shadow_proxies)
                if (candidate.error <= proxy_error)
                    proxy = &candidate;
            if (proxy)
            {
                if (proxy->indices.index_count == 0)
                    continue;
                draw.mesh_index = proxy->mesh_index;
                draw.indices = proxy->indices;
            }
            draws.push_back(draw);
        }

        const int32_t x = int32_t(cascade % Constants::shadow_atlas_tiles * tile);
        const int32_t y = int32_t(cascade / Constants::shadow_atlas_tiles * tile);
        ctx.backend.set_viewport(ctx.cmd, x, y, tile, tile);
        ctx.backend.clear_depth(ctx.cmd, x, y, tile, tile);
        // rebind: draw_items pushes the cascade index when it binds a pipeline
        ctx.current_pipeline = nullptr;
        draw_items(ctx, draws, cascade, false, [&] (const RenderPrimitivePassInfo& info)
        {
            bind_shadow_globals(ctx);

            // alpha tested shadow pipelines sample material textures
            if (info.pipeline_family->uses_resource("pbr_material_table"))
                ctx.bind(pbr_material_table_resource);
            if (info.pipeline_family->uses_resource("textures"))
                ctx.bind(textures_resource);
        });
    }
    ctx.backend.set_viewport(ctx.cmd, 0, 0, Constants::shadowmap_extent.width, Constants::shadowmap_extent.height);
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

void GenericRenderGraph::add_debug_overlay_pass()
{
    add_pass({
        .name = "DebugOverlay",
        .writes = {
            { swapchain_color, RBImageUsageType::ColorAttachment, RBLoadOp::Load },
            // depth tested wireframe
            { gbuffer[GBUFFER_SLOTS::DEPTH], RBImageUsageType::DepthStencilAttachment, RBLoadOp::Load },
        },
        .execute = [this](RenderGraphContext& ctx)
        {
            draw_debug_overlay(ctx);
        },
    });
}

void GenericRenderGraph::draw_debug_overlay(RenderGraphContext& ctx)
{
    PROFILE("GenericRenderGraph::draw_debug_overlay");

    // taken every frame, so lines don't pile up while nothing is drawn
    debug_draw::collect(debug_draw_frame);

    const bool draw_wireframe = is_wireframe_view_mode(view_mode) && wireframe_mesh_pipeline;
    const bool draw_skeleton = show_skeleton && skeleton_pipeline;
    const bool draw_lines = !debug_draw_frame.depth_tested.empty() || !debug_draw_frame.on_top.empty();

    if (!draw_wireframe && !draw_skeleton && !draw_lines)
        return;

    ctx.backend.update_viewport(
        ctx.cmd,
        resolution,
        use_swapchain_extent);

    if (draw_wireframe)
        draw_mesh_wireframe(ctx);

    draw_debug_lines(ctx);
}

void GenericRenderGraph::draw_mesh_wireframe(RenderGraphContext& ctx)
{
    auto& mesh_processor = engine->scene_view->get_processor<SceneViewProcessor_Mesh>();

    const uint32_t first_command = draw_list.command_count();
    for (const RenderPrimitive& prim : mesh_processor.primitives)
    {
        // only primitives drawn by the scene passes (skips released slots)
        if (!prim.get_pass_info(Names::pass_geometry_base) && !prim.get_pass_info(Names::pass_geometry_translucent))
            continue;
        if (!prim.in_lod_range(glm::vec3(current_camera_ubo.camera_pos)))
            continue;

        // wireframe_mesh.vert: 3 edges (6 line vertices) per triangle
        draw_list.add_vertex_draw({ .mesh_id = (uint32_t)prim.mesh_index, .primitive_id = prim.id, .material_id = 0 },
            (uint32_t)prim.mesh.get().indices.size() * 2);
    }

    if (ctx.bind_pipeline(wireframe_mesh_pipeline))
    {
        ctx.bind(camera_resource, mesh_table_resource, primitive_table_resource);
        draw_list.bind(ctx);
    }
    ctx.push_constants(ModelPushConstants{ .debug_id = 0 });
    draw_list.draw_vertex_indirect(ctx, first_command, draw_list.command_count() - first_command);
}

void GenericRenderGraph::add_skeleton_lines(std::vector<LineVertex>& vertices)
{
    auto& mesh_processor = engine->scene_view->get_processor<SceneViewProcessor_Mesh>();

    const glm::vec4 bone_color_parent = { 1.0f, 0.45f, 0.05f, 1.0f };
    const glm::vec4 bone_color_child = { 1.0f, 0.95f, 0.55f, 1.0f };
    const glm::vec4 axis_colors[3] = {
        { 1.0f, 0.15f, 0.15f, 1.0f },
        { 0.15f, 1.0f, 0.15f, 1.0f },
        { 0.25f, 0.45f, 1.0f, 1.0f },
    };

    auto add_line = [&vertices](const glm::vec3& a, const glm::vec3& b, const glm::vec4& color_a, const glm::vec4& color_b)
    {
        vertices.push_back({ a, 0.0f, color_a });
        vertices.push_back({ b, 0.0f, color_b });
    };

    // all primitives of a skeletal mesh share one pose
    std::set<const SkinningPose*> drawn_poses;
    std::vector<glm::mat4> bone_world;

    for (const RenderPrimitive& prim : mesh_processor.primitives)
    {
        if (!prim.skinning || !drawn_poses.insert(prim.skinning.get()).second)
            continue;

        const SkinningPose& pose = *prim.skinning;
        if (!pose.mesh.is_valid())
            continue;

        const Skeleton& skeleton = pose.mesh.get().skeleton;
        const uint32_t num_bones = skeleton.num_bones();
        if (num_bones == 0 || pose.skinning_matrices.size() != num_bones)
            continue;

        // skinning = bone_global * inverse_bind  ->  bone_global = skinning * bind
        bone_world.resize(num_bones);
        for (uint32_t bone = 0; bone < num_bones; ++bone)
            bone_world[bone] = *prim.world * pose.skinning_matrices[bone] * glm::inverse(skeleton.bones[bone].inverse_bind);

        // joint axes scale with the skeleton: a quarter of the average bone length
        float total_bone_length = 0.0f;
        uint32_t num_bone_segments = 0;
        for (uint32_t bone = 0; bone < num_bones; ++bone)
        {
            const int32_t parent = skeleton.bones[bone].parent;
            if (parent < 0)
                continue;
            total_bone_length += glm::distance(glm::vec3(bone_world[bone][3]), glm::vec3(bone_world[parent][3]));
            num_bone_segments++;
        }
        const float axis_length = num_bone_segments > 0 ? 0.25f * total_bone_length / (float)num_bone_segments : 0.05f;

        for (uint32_t bone = 0; bone < num_bones; ++bone)
        {
            const glm::mat4& m = bone_world[bone];
            const glm::vec3 position = glm::vec3(m[3]);
            const int32_t parent = skeleton.bones[bone].parent;

            // bone: parent joint (orange) -> child joint (light yellow)
            if (parent >= 0)
                add_line(glm::vec3(bone_world[parent][3]), position, bone_color_parent, bone_color_child);

            // joint orientation: X red, Y green, Z blue; roots get longer axes
            const float length = parent >= 0 ? axis_length : axis_length * 2.5f;
            for (int axis = 0; axis < 3; ++axis)
            {
                const glm::vec3 dir = glm::vec3(m[axis]);
                const float dir_length = glm::length(dir);
                if (dir_length < 1e-6f)
                    continue;
                add_line(position, position + dir * (length / dir_length), axis_colors[axis], axis_colors[axis]);
            }
        }
    }
}

void GenericRenderGraph::draw_debug_lines(RenderGraphContext& ctx)
{
    // one upload for all line lists (the buffer is written once per frame), drawn as ranges:
    // skeletons and on-top lines without depth test, then depth tested lines
    std::vector<LineVertex>& vertices = debug_line_vertices;
    vertices.clear();

    if (show_skeleton && skeleton_pipeline)
        add_skeleton_lines(vertices);

    auto add_lines = [&vertices] (const std::vector<debug_draw::Line>& lines)
    {
        for (const debug_draw::Line& l : lines)
        {
            vertices.push_back({ l.a, 0.0f, l.color });
            vertices.push_back({ l.b, 0.0f, l.color });
        }
    };

    if (skeleton_pipeline)
        add_lines(debug_draw_frame.on_top);
    const uint32_t on_top_count = (uint32_t)vertices.size();

    if (debug_lines_pipeline)
        add_lines(debug_draw_frame.depth_tested);

    if (vertices.empty())
        return;

    // whole lines only
    const uint32_t vertex_count = std::min<uint32_t>((uint32_t)vertices.size(), debug_line_capacity & ~1u);

    auto* dst = static_cast<LineVertex*>(backend->get_vertex_buffer_ptr(debug_line_buffer, ctx.frame));
    memcpy(dst, vertices.data(), vertex_count * sizeof(LineVertex));

    const uint32_t first_count = std::min(on_top_count, vertex_count);
    if (first_count > 0)
    {
        if (ctx.bind_pipeline(skeleton_pipeline))
            ctx.bind(camera_resource);
        backend->bind_vertex_buffer(ctx.cmd, debug_line_buffer, ctx.frame);
        ctx.draw(first_count);
    }

    if (vertex_count > first_count)
    {
        if (ctx.bind_pipeline(debug_lines_pipeline))
            ctx.bind(camera_resource);
        backend->bind_vertex_buffer(ctx.cmd, debug_line_buffer, ctx.frame);
        ctx.draw(vertex_count - first_count, first_count);
    }
}

void GenericRenderGraph::draw_ssr(RenderGraphContext& ctx)
{
    PROFILE("GenericRenderGraph::draw_ssr");

    // the SSR target may be smaller than the screen (render.ssr.downscale)
    const Extent ssr_extent = textures[ssr_texture.id].desc.extent;
    if (ssr_extent.is_zero())
        ctx.backend.update_viewport(ctx.cmd, resolution, use_swapchain_extent);
    else
        ctx.backend.set_viewport(ctx.cmd, 0, 0, ssr_extent.width, ssr_extent.height);
    
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
        ctx.bind(camera_resource, gbuffer_resource, hdr_color_output_resource, ssr_resource, reflection_resource);
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
    pc.spp = std::min(ctx.params.render_iter_id * 10 + 1, 20u);

    ctx.backend.push_constants(
        ctx.cmd,
        pc
    );
    
    ctx.trace_rays(rtx_gi_pipeline, resolution, 1);

}

void GenericRenderGraph::add_copy_pass(Name name, RGTextureHandle src, RGTextureHandle dst, bool ping_pong,
    std::function<bool()> condition)
{
    const uint32_t num_layers = ping_pong ? 2 : 1;
    add_pass({
        .name = name,
        .condition = std::move(condition),
        .reads = {
            { src, RBImageUsageType::TransferSrc }
        },
        .writes = {
            { dst, RBImageUsageType::TransferDst, RBLoadOp::Load }
        },
        .execute = [this, src, dst, ping_pong](RenderGraphContext& ctx)
        {
            if (ping_pong && ctx.level != history_index)
                return;
    
            
            CopyImageParams params;
            params.source = get_image(src);
            params.dest = get_image(dst);
            params.dst_layer = ctx.level;
            ctx.copy_img(params);
        },
        .num_layers = num_layers,
        .type = RenderPassType::transfer
    });
}

void GenericRenderGraph::prepare_nn_denoiser_passes()
{
    // auto pipeline_json = parse_json("nn/pipeline.json");
    // for (auto& p : pipeline_json["passes"]) {
    //     add_pass({
    //         .name = p["name"],
    //         .reads = resolve_reads(p),
    //         .writes = resolve_writes(p),
    //         .execute = make_executor(p),
    //         .type = RenderPassType::compute
    //     });
    // }
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
