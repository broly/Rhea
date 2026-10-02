module game;

import :render_graph;

import std.compat;
import assertions;

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
import physics;
import ecs;
import gameplay;

#include "render_layout.h"
#include "common/assertion_macros.h"
#include "profiling/profile.h"


GameRenderGraph::GameRenderGraph()
{
    allow_shadow_debug = false;
    
    num_pass_instances = 1;
    capture_dimension = TextureDimension::Tex2D;
    resolution = Constants::zero_extent;
    use_swapchain_extent = true;
    taa_supported = true;
}

void GameRenderGraph::init_resources(const std::map<Name, bool>& init_params)
{
    set_flag(Names::debug_shadow2, false);
    GenericRenderGraph::init_resources(init_params);

    post_renderer = std::make_unique<PostProcessRenderer>();
    post_renderer->init(*renderer, *backend);
    for (uint32_t level = 0; level < PostProcessRenderer::bloom_levels; ++level)
    {
        bloom_down[level] = create_texture({
            .name = Name(std::format("bloom_down_{}", level)),
            .extent_divisor = 2u << level,
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
        });
        if (level + 1 < PostProcessRenderer::bloom_levels)
            bloom_up[level] = create_texture({
                .name = Name(std::format("bloom_up_{}", level)),
                .extent_divisor = 2u << level,
                .format = TextureFormat::RGBA16F,
                .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
            });
    }
    // RGBA32F: the adaptation moves the exposure by thousandths of an EV per frame
    exposure = create_texture({
        .name = "post_exposure",
        .extent = PostProcessRenderer::exposure_extent,
        .format = TextureFormat::RGBA32F,
        .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
    });

    jpeg_renderer = std::make_unique<JpegRenderer>();
    jpeg_renderer->init(*renderer, *backend);
    // divisor 1: the screen size, kept through resizes (the shakalizer reads the extent)
    ldr_color = create_texture({
        .name = "ldr_color",
        .extent_divisor = 1,
        .format = TextureFormat::RGBA8_UNORM,
        .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
    });
    screen_jpeg_color = create_texture({
        .name = "screen_jpeg",
        .extent_divisor = 1,
        .format = TextureFormat::RGBA8_UNORM,
        .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
    });
    // one texel per 8 x 8 pixels (JPEG_MASK_BLOCK), a channel per hit slot
    hit_mask = create_texture({
        .name = "jpeg_hit_mask",
        .extent_divisor = 8,
        .format = TextureFormat::RGBA8_UNORM,
        .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
    });
    hit_jpeg_work = create_texture({
        .name = "hit_jpeg_work",
        .extent_divisor = 1,
        .format = TextureFormat::RGBA8_UNORM,
        .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
    });
    hit_jpeg_color = create_texture({
        .name = "hit_jpeg",
        .extent_divisor = 1,
        .format = TextureFormat::RGBA8_UNORM,
        .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
    });
}

void GameRenderGraph::build_passes(const std::map<Name, bool>& parameters)
{
    GenericRenderGraph::build_passes(parameters);

    add_taa_passes();
    add_post_process_passes();

    add_pass({
            .name = "ToneMapping",
            .condition = [this] () { return !is_debugging(); },
            .reads = {
                { hdr_color_present[COLOR_OUTPUT_HDR::BASE], RBImageUsageType::SampledFragment },
                { bloom_up[0], RBImageUsageType::SampledFragment },
                { exposure, RBImageUsageType::SampledFragment },
                { hdr_color_history[COLOR_OUTPUT_HDR::BASE], RBImageUsageType::SampledFragment },                
                { hdr_color_history[COLOR_OUTPUT_HDR::RTXGI], RBImageUsageType::SampledFragment },                
                { hdr_color_history[COLOR_OUTPUT_HDR::RTXGI_ACCUM], RBImageUsageType::SampledFragment },                
                { hdr_color_history[COLOR_OUTPUT_HDR::RTXGI_FILTERED], RBImageUsageType::SampledFragment },        
                { hdr_color_history[COLOR_OUTPUT_HDR::RTXGI_NEURAL_DENOISED], RBImageUsageType::SampledFragment },
                { gtao_result, RBImageUsageType::SampledFragment },
            },
            .writes = {
                { ldr_color, RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                if (ctx.bind_pipeline(tonemap_pipeline))
                {
                    ctx.bind(
                        hdr_color_output_resource, 
                        gbuffer_resource,
                        gtao_resource);
                    if (nn_denoiser_state.initialized)
                    {
                        ctx.bind(nn_denoiser_state.resource);
                    }
                    post_renderer->bind_tonemap(ctx);
                }
                const float time = RhGlobals::engine->world->get_time_seconds();
                const float delta = RhGlobals::engine->world->get_delta_seconds();
                // const uint32_t fps = 1 / delta;
                const uint32_t avg_fps = (uint32_t)RhGlobals::engine->world->get_avg_fps();
                uint32_t mode = ctx.params.get_int("output_mode", 0);
                // GI debug views (1-3) show ray tracing outputs
                if constexpr (!render_settings::enable_raytracing)
                    mode = 0;
                TonemapPushConstants pc {time, mode, avg_fps, (uint32_t)view_mode};
                ctx.push_constants(pc);
                         
                ctx.backend.draw_fullscreen(ctx.cmd);
                
            },
    });
    
    // full screen JPEG damage (screen_jpeg): only while the player is hurt or hit
    jpeg_renderer->add_chain(*this, {
        .name = "ScreenJpeg",
        .source = ldr_color,
        .target = screen_jpeg_color,
        .condition = [this] () { return !is_debugging() && screen_jpeg_state.active; },
        .settings = [this] () { return screen_jpeg_state.settings; },
    });

    // JPEG spots of the hits (jpeg_hits): only while a hit lives, a chain per slot (the preset of a weapon)
    jpeg_renderer->add_hit_mask_pass(*this, hit_mask, gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], camera_resource,
        gbuffer_resource, [this] () { return !is_debugging() && hits_frame.any; });
    for (uint32_t slot = 0; slot < jpeg_hits::slot_count; ++slot)
    {
        jpeg_renderer->add_chain(*this, {
            .name = Name(std::format("HitJpeg{}", slot)),
            .source = ldr_color,
            .target = hit_jpeg_color,
            .condition = [this, slot] () { return !is_debugging() && hits_frame.slot_active[slot]; },
            .settings = [this, slot] () { return hits_frame.slot_settings[slot]; },
            .mask = hit_mask,
            .mask_channel = slot,
            .work = hit_jpeg_work,
        });
    }

    add_pass({
        .name = "Present",
        .condition = [this] () { return !is_debugging(); },
        .reads = {
            { ldr_color, RBImageUsageType::SampledFragment },
            { screen_jpeg_color, RBImageUsageType::SampledFragment },
            { hit_jpeg_color, RBImageUsageType::SampledFragment },
            { hit_mask, RBImageUsageType::SampledFragment },
        },
        .writes = {
            { swapchain_color, RBImageUsageType::ColorAttachment, RBLoadOp::Clear }
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            jpeg_renderer->draw_present(ctx, screen_jpeg_state.active ? screen_jpeg_state.amount : 0.0f, hits_frame.any);
        },
    });

    // wireframe / skeleton on top of the tonemapped image
    add_debug_overlay_pass();

    // F9: intermediate buffers of one frame -> cache/debug_dump/<buffer>/frame_<N>.exr.
    // Last pass on purpose, and skipped (with its barriers) unless requested: the frame is unchanged otherwise.
    std::vector<ExrDumpEntry> dump_entries = {
        { .texture = gbuffer[GBUFFER_SLOTS::WORLD_NORMAL], .subdir = "g_world_normal" },
        { .texture = gbuffer[GBUFFER_SLOTS::LINEAR_DEPTH], .subdir = "g_linear_depth" },
        { .texture = gbuffer[GBUFFER_SLOTS::ALBEDO_ROUGHNESS], .subdir = "g_albedo_roughness" },
        { .texture = gbuffer[GBUFFER_SLOTS::MOTION_VECTORS], .subdir = "g_motion_vectors", .out_channels = 3 },
        { .texture = gbuffer[GBUFFER_SLOTS::GEOMETRY_NORMAL], .subdir = "g_geometry_normal" },
        { .texture = gbuffer[GBUFFER_SLOTS::EMISSIVE], .subdir = "g_emissive" },
        { .texture = decal_albedo, .subdir = "decal_albedo" },
        { .texture = hdr_color_present[COLOR_OUTPUT_HDR::BASE], .subdir = "hdr_base" },
        { .texture = hdr_color_present[COLOR_OUTPUT_HDR::INTERMEDIATE], .subdir = "hdr_intermediate" },
    };
    // emissive watch ring (GenericRenderGraph::EMISSIVE_RING_SIZE layers, the log names the corrupted one)
    if (emissive_diagnostics_enabled())
        for (uint32_t layer = 0; layer < EMISSIVE_RING_SIZE; ++layer)
            dump_entries.push_back({ .texture = emissive_ring, .subdir = "g_emissive_ring_" + std::to_string(layer), .layer = layer });

    add_exr_dump_pass({
        .name = "DebugDumpFrame",
        .subdir = "debug_dump",
        .entries = std::move(dump_entries),
        .condition = [] (const RenderGraphParameters&) { return true; },
        .pass_condition = [this] ()
        {
            auto it = one_time_render_flags.find("debug_dump_frame");
            return it != one_time_render_flags.end() && it->second;
        },
    });
}

void GameRenderGraph::prepare_resources(RenderGraphContext& ctx)
{

    uint32_t prev_layer = history_index ^ 1;
    
    GenericRenderGraph::prepare_resources(ctx);
    
     
    
    gbuffer_resource->update_image_array("u_gbuffer", get_image_array(gbuffer), 
    {.frame = ctx.frame});
    gbuffer_resource->update_image_array("u_gbuffer_hist", get_image_array(gbuffer_hist), 
    {.frame = ctx.frame, .layer_index = prev_layer});
    dbuffer_resource->update_image("u_decal_albedo", get_image(decal_albedo),
        {.frame = ctx.frame});
    
    hdr_color_output_resource->update_image_array(
        "u_hdr_color_present", get_image_array(hdr_color_present), 
        {.frame = ctx.frame});
    
    hdr_color_output_resource->update_image_array(
        "u_hdr_color_history", get_image_array(hdr_color_history), 
        {.frame = ctx.frame, .layer_index = prev_layer});

    PostProcessRenderer::FrameInputs post_inputs{
        .scene = get_image(hdr_color_present[COLOR_OUTPUT_HDR::BASE]),
        .exposure = get_image(exposure),
        .screen = resolution.is_zero() ? backend->get_swapchain_extent() : resolution,
        .delta_seconds = (float)RhGlobals::engine->world->get_delta_seconds(),
    };
    for (uint32_t level = 0; level < PostProcessRenderer::bloom_levels; ++level)
        post_inputs.down[level] = get_image(bloom_down[level]);
    for (uint32_t level = 0; level + 1 < PostProcessRenderer::bloom_levels; ++level)
        post_inputs.up[level] = get_image(bloom_up[level]);
    post_renderer->prepare(ctx, post_inputs);

    const float jpeg_delta = (float)RhGlobals::engine->world->get_delta_seconds();
    screen_jpeg::tick(jpeg_delta);
    screen_jpeg_state = screen_jpeg::evaluate();
    run_camera_commands();
    jpeg_hits::tick(jpeg_delta);
    hits_frame = jpeg_hits::update();
    jpeg_renderer->prepare(ctx, *this);
    jpeg_renderer->prepare_hit_mask(ctx, get_image(hit_mask), hits_frame.ubo);
    jpeg_renderer->prepare_present(ctx, get_image(ldr_color), get_image(screen_jpeg_color), get_image(hit_jpeg_color),
        get_image(hit_mask));

    auto shadow_debug_instance = shadow_resource->query_single();
    
    shadow_debug_instance->update_image(
        "u_shadow_depth",
        get_image(shadow_map),
        {
            .frame = ctx.frame
        }
    );
    
    shadow_debug_instance->update_image(
        "u_shadow_depth_debug",
        get_image(shadow_map),
        {
            .frame = ctx.frame
        }
    );
}

std::optional<phys::Hit> GameRenderGraph::camera_center_hit() const
{
    const glm::vec3 origin = glm::vec3(current_camera_ubo.camera_pos);
    const glm::vec3 forward = glm::normalize(glm::vec3(current_camera_ubo.inv_view * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    phys::QueryFilter filter;
    filter.categories = phys::Category::static_world | phys::Category::dynamic;
    return RhGlobals::engine->world->get_physics().raycast(origin, forward, 500.0f, filter);
}

void GameRenderGraph::run_camera_commands()
{
    if (auto request = jpeg_hits::take_shoot_request())
    {
        if (auto hit = camera_center_hit())
            jpeg_hits::add(hit->position, request->preset, request->strength, request->radius, request->lifetime);
    }
    if (auto request = damage_feedback::take_look_request())
    {
        if (auto hit = camera_center_hit())
        {
            // framework convention: BodyDesc::user_data is the owning entity (ecs::Entity::bits), or 0
            const ecs::Entity target = hit->user_data != 0
                ? ecs::Entity{ uint32_t(hit->user_data), uint32_t(hit->user_data >> 32) }
                : ecs::null_entity;
            const glm::vec3 origin = glm::vec3(current_camera_ubo.camera_pos);
            RhGlobals::engine->world->registry.events<DamageEvent>().send({
                .target = target,
                .amount = request->amount,
                .type = DamageType::Hitscan,
                .point = hit->position,
                .direction = glm::normalize(hit->position - origin),
                .jpeg_preset = request->preset,
            });
        }
    }
}

void GameRenderGraph::add_post_process_passes()
{
    // the auto exposure meters a level of the down chain: the levels up to it run for either
    const std::function<bool()> chain_condition = [this] ()
    {
        return !is_debugging() && (post_renderer->bloom_enabled() || post_renderer->auto_exposure_enabled());
    };
    const std::function<bool()> bloom_condition = [this] () { return !is_debugging() && post_renderer->bloom_enabled(); };

    for (uint32_t level = 0; level < PostProcessRenderer::bloom_levels; ++level)
    {
        const RGTextureHandle source = level == 0 ? hdr_color_present[COLOR_OUTPUT_HDR::BASE] : bloom_down[level - 1];
        add_pass({
            .name = Name(std::format("BloomDown{}", level)),
            .condition = level <= PostProcessRenderer::exposure_level ? chain_condition : bloom_condition,
            .reads = {
                { source, RBImageUsageType::Sampled },
            },
            .writes = {
                { bloom_down[level], RBImageUsageType::StorageImage }
            },
            .execute = [this, level] (RenderGraphContext& ctx)
            {
                PROFILE("BloomDown");
                post_renderer->dispatch_downsample(ctx, level, textures[bloom_down[level].id].desc.extent);
            },
            .type = RenderPassType::compute
        });
    }

    add_pass({
        .name = "AutoExposure",
        .condition = [this] () { return !is_debugging() && post_renderer->auto_exposure_enabled(); },
        .reads = {
            { bloom_down[PostProcessRenderer::exposure_level], RBImageUsageType::Sampled },
        },
        .writes = {
            { exposure, RBImageUsageType::StorageImage }
        },
        .execute = [this] (RenderGraphContext& ctx)
        {
            PROFILE("AutoExposure");
            post_renderer->dispatch_auto_exposure(ctx);
        },
        .type = RenderPassType::compute
    });

    for (int level = (int)PostProcessRenderer::bloom_levels - 2; level >= 0; --level)
    {
        // the smallest up level starts from the smallest down level
        const RGTextureHandle low = level + 2 == (int)PostProcessRenderer::bloom_levels
            ? bloom_down[PostProcessRenderer::bloom_levels - 1]
            : bloom_up[level + 1];
        add_pass({
            .name = Name(std::format("BloomUp{}", level)),
            .condition = bloom_condition,
            .reads = {
                { low, RBImageUsageType::Sampled },
                { bloom_down[level], RBImageUsageType::Sampled },
            },
            .writes = {
                { bloom_up[level], RBImageUsageType::StorageImage }
            },
            .execute = [this, level] (RenderGraphContext& ctx)
            {
                PROFILE("BloomUp");
                post_renderer->dispatch_upsample(ctx, (uint32_t)level, textures[bloom_up[level].id].desc.extent);
            },
            .type = RenderPassType::compute
        });
    }
}

void GameRenderGraph::pass_shadow_map(RenderGraphContext& ctx)
{
    draw_scene_shadow(ctx);
}

void GameRenderGraph::pass_shadow_debug(RenderGraphContext& ctx)
{
    if (ctx.bind_pipeline(shadow_debug_pipeline))
    {
        ctx.bind(shadow_resource);
    }
    
    ctx.backend.draw_fullscreen(ctx.cmd);
}

void GameRenderGraph::pass_translucent(RenderGraphContext& ctx)
{
    PROFILE("Translucent");
            
    draw_scene(ctx);
}

void GameRenderGraph::pass_tonemapping(RenderGraphContext& ctx)
{
            
   
}




void GameRenderGraph::pass_readback(RenderGraphContext& ctx)
{
    
}