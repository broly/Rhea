module game;

import :jpeg;

import std.compat;
import assertions;

import render;
import glm;
import name;
import rhmath;
import profile;
import texture_format;

#include "common/assertion_macros.h"
#include "profiling/profile.h"

namespace
{
    // local size of jpeg_downscale.comp, jpeg_output.comp and jpeg_composite.comp
    constexpr uint32_t group = 8;

    // jpeg_flag_* of JpegPushConstants::mode.w (JPEG_FLAG_* in shaders/resources/jpeg.glsl)
    constexpr int jpeg_flag_source_linear = 1;
    constexpr int jpeg_flag_target_linear = 2;
    constexpr int jpeg_flag_block_list = 4;
    constexpr int jpeg_flag_channel_shift = 8;

    // local size of jpeg_blocks.comp
    constexpr uint32_t blocks_group = 64;
    // the largest grid shift of a generation + 1 (jpeg::grid_shift)
    constexpr int max_shift = 8;

    ComputeWorkgroups groups_for(Extent extent)
    {
        return { (extent.width + group - 1) / group, (extent.height + group - 1) / group, 1 };
    }

    // pixels of an MCU per axis: 8 x the chroma subsampling
    glm::ivec2 mcu_size(JpegChroma chroma)
    {
        switch (chroma)
        {
            case JpegChroma::Yuv422: return { 16, 8 };
            case JpegChroma::Yuv420: return { 16, 16 };
            case JpegChroma::Yuv411: return { 32, 8 };
            default: return { 8, 8 };
        }
    }

    // lowbias32 (Chris Wellons): a well mixed 32 bit hash
    uint32_t hash32(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }
}

/************************************************************************
 * SETTINGS
 ***********************************************************************/

JpegSettings jpeg::clamped(const JpegSettings& settings)
{
    JpegSettings result = settings;
    result.downscale = std::clamp(settings.downscale, 1, max_downscale);
    result.downscale_x = settings.downscale_x > 0 ? std::clamp(settings.downscale_x, 1, max_downscale) : result.downscale;
    result.quality = std::clamp(settings.quality, 1, 100);
    result.generations = std::clamp(settings.generations, 1, max_generations);
    result.sharpen = std::clamp(settings.sharpen, 0.0f, 1.0f);
    result.mask_quality = std::clamp(settings.mask_quality, 1, 100);
    result.brightness = std::clamp(settings.brightness, -1.0f, 4.0f);
    result.noise = std::clamp(settings.noise, 0.0f, 1.0f);
    if ((uint8_t)settings.chroma > (uint8_t)JpegChroma::Yuv411)
        result.chroma = JpegChroma::Yuv420;
    if ((uint8_t)settings.filter > (uint8_t)JpegFilter::Box)
        result.filter = JpegFilter::Box;
    return result;
}

glm::ivec2 jpeg::grid_shift(uint32_t seed, int generation)
{
    if (generation <= 0)
        return { 0, 0 };
    const uint32_t h = hash32(seed * 0x9e3779b9u + (uint32_t)generation);
    return { 1 + (int)(h % 7u), 1 + (int)((h >> 8) % 7u) };
}

/************************************************************************
 * RENDERER
 ***********************************************************************/

void JpegRenderer::init(Renderer& in_renderer, RenderBackend& in_backend)
{
    renderer = &in_renderer;
    backend = &in_backend;

    resource = renderer->find_resource("jpeg");
    checkf(resource, "resource 'jpeg' is missing (assets/render/resources/jpeg.json)");
    blocks_resource = renderer->find_resource("jpeg_blocks");
    checkf(blocks_resource, "resource 'jpeg_blocks' is missing (assets/render/resources/jpeg_blocks.json)");

    auto model = renderer->find_model("jpeg");
    checkf(model, "material model 'jpeg' is missing (assets/render/schemas/jpeg.json)");
    downscale_family = renderer->query_pipeline_family("JpegDownscale", model);
    codec_family = renderer->query_pipeline_family("JpegCodec", model);
    output_family = renderer->query_pipeline_family("JpegOutput", model);
    composite_family = renderer->query_pipeline_family("JpegComposite", model);
    blocks_family = renderer->query_pipeline_family("JpegBlocks", model);
    hit_mask_family = renderer->query_pipeline_family("JpegHitMask", model);
}

void JpegRenderer::add_chain(RenderGraph& graph, JpegChainDesc desc)
{
    checkf(resource, "JpegRenderer::init was not called");
    checkf(desc.settings, "jpeg chain %s has no settings", desc.name.to_string().c_str());

    // a copy: create_texture grows graph.textures
    const Extent extent = graph.textures[desc.source.id].desc.extent;
    const uint32_t extent_divisor = graph.textures[desc.source.id].desc.extent_divisor;

    auto chain = std::make_unique<Chain>();
    chain->desc = std::move(desc);
    chain->downscale_instance = next_instance++;
    chain->codec_instance = next_instance++;
    chain->output_instance = next_instance++;
    if (chain->desc.mask)
    {
        chain->blocks_instance = next_instance++;
        chain->block_list = next_block_list++;
    }
    // the size of the source: a downscale uses its top left corner
    chain->work = chain->desc.work ? *chain->desc.work : graph.create_texture({
        .name = Name(std::format("{}_work", chain->desc.name.to_string())),
        .extent = extent,
        .extent_divisor = extent_divisor,
        .format = TextureFormat::RGBA8_UNORM,
        .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
    });

    Chain* c = chain.get();
    const std::string prefix = c->desc.name.to_string();
    std::function<bool()> condition = c->desc.condition;

    // a masked downscale scales its brightness and noise by the mask
    std::vector<RBImageUsage> downscale_reads = { { c->desc.source, RBImageUsageType::Sampled } };
    if (c->desc.mask)
        downscale_reads.push_back({ *c->desc.mask, RBImageUsageType::StorageImage });
    graph.add_pass({
        .name = Name(prefix + "Downscale"),
        .condition = condition,
        .reads = std::move(downscale_reads),
        .writes = {
            { c->work, RBImageUsageType::StorageImage }
        },
        .execute = [this, c] (RenderGraphContext& ctx) { execute_downscale(ctx, *c); },
        .type = RenderPassType::compute
    });
    if (c->desc.mask)
    {
        graph.add_pass({
            .name = Name(prefix + "Blocks"),
            .condition = condition,
            .reads = {
                { *c->desc.mask, RBImageUsageType::StorageImage },
            },
            .execute = [this, c] (RenderGraphContext& ctx) { execute_blocks(ctx, *c); },
            .type = RenderPassType::compute
        });
    }
    graph.add_pass({
        .name = Name(prefix + "Codec"),
        .condition = condition,
        .writes = {
            { c->work, RBImageUsageType::StorageImage }
        },
        .execute = [this, c] (RenderGraphContext& ctx) { execute_codec(ctx, *c); },
        .type = RenderPassType::compute
    });
    // a masked output writes only the blocks its channel wins
    std::vector<RBImageUsage> output_reads = { { c->work, RBImageUsageType::StorageImage } };
    if (c->desc.mask)
        output_reads.push_back({ *c->desc.mask, RBImageUsageType::StorageImage });
    graph.add_pass({
        .name = Name(prefix + "Output"),
        .condition = condition,
        .reads = std::move(output_reads),
        .writes = {
            { c->desc.target, RBImageUsageType::StorageImage }
        },
        .execute = [this, c] (RenderGraphContext& ctx) { execute_output(ctx, *c); },
        .type = RenderPassType::compute
    });

    chains.push_back(std::move(chain));
}

void JpegRenderer::prepare(RenderGraphContext& ctx, RenderGraph& graph)
{
    PROFILE("JpegRenderer::prepare");

    // pipelines are requested every frame: hot reload drops the PSO cache
    downscale_pipeline = downscale_family->request_pipeline({});
    codec_pipeline = codec_family->request_pipeline({});
    output_pipeline = output_family->request_pipeline({});
    composite_pipeline = composite_family->request_pipeline({});
    blocks_pipeline = blocks_family->request_pipeline({});
    hit_mask_pipeline = hit_mask_family->request_pipeline({});

    const UpdateImageParams params{ .frame = ctx.frame };
    for (auto& chain : chains)
    {
        chain->settings = jpeg::clamped(chain->desc.settings());

        const RBImageHandle work = graph.get_image(chain->work);
        resource->update_image("u_jpeg_source", graph.get_image(chain->desc.source), params, chain->downscale_instance);
        resource->update_image("u_jpeg_work", work, params, chain->downscale_instance);
        resource->update_image("u_jpeg_work", work, params, chain->codec_instance);
        resource->update_image("u_jpeg_work", work, params, chain->output_instance);
        resource->update_image("u_jpeg_target", graph.get_image(chain->desc.target), params, chain->output_instance);
        if (chain->desc.mask)
        {
            const RBImageHandle mask = graph.get_image(*chain->desc.mask);
            resource->update_image("u_jpeg_mask", mask, params, chain->blocks_instance);
            resource->update_image("u_jpeg_mask", mask, params, chain->output_instance);
            resource->update_image("u_jpeg_mask", mask, params, chain->downscale_instance);
        }
    }
}

const JpegSettings& JpegRenderer::get_chain_settings(Name name) const
{
    for (const auto& chain : chains)
        if (chain->desc.name == name)
            return chain->settings;
    unreachable("no jpeg chain named %s", name.to_string().c_str());
}

Extent JpegRenderer::source_extent(const RenderGraph& graph, const Chain& chain) const
{
    const Extent extent = graph.textures[chain.desc.source.id].desc.extent;
    // a texture without an extent has the swapchain's
    return extent.is_zero() ? backend->get_swapchain_extent() : extent;
}

JpegPushConstants JpegRenderer::make_push_constants(const RenderGraph& graph, const Chain& chain) const
{
    const JpegSettings& s = chain.settings;
    const Extent source = source_extent(graph, chain);
    const glm::ivec2 factor = { s.downscale_x, s.downscale };
    const glm::ivec2 work = {
        ((int)source.width + factor.x - 1) / factor.x,
        ((int)source.height + factor.y - 1) / factor.y,
    };
    const int flags = (chain.desc.source_linear ? jpeg_flag_source_linear : 0)
        | (chain.desc.target_linear ? jpeg_flag_target_linear : 0)
        | (chain.desc.mask ? jpeg_flag_block_list | (int)(chain.desc.mask_channel & 3u) << jpeg_flag_channel_shift : 0);

    return JpegPushConstants{
        .extent = glm::ivec4(work, (int)source.width, (int)source.height),
        .mode = glm::ivec4(factor.x | factor.y << 8, (int)s.filter, (int)s.chroma, flags),
        .codec = glm::ivec4(s.quality, 0, 0, 0),
        .post = glm::vec4(s.sharpen, 0.0f, (float)s.mask_quality, 0.0f),
    };
}

void JpegRenderer::execute_downscale(RenderGraphContext& ctx, Chain& chain)
{
    PROFILE("JpegRenderer::downscale");

    JpegPushConstants pc = make_push_constants(ctx.render_graph, chain);
    // brightness and noise before the codec, the noise new every frame
    pc.post.y = chain.settings.brightness;
    pc.post.w = chain.settings.noise;
    pc.codec.y = int(++noise_frame);
    ctx.bind_pipeline(downscale_pipeline);
    ctx.bind(resource->query_single(chain.downscale_instance));
    ctx.push_constants(pc);
    ctx.compute(groups_for({ (uint32_t)pc.extent.x, (uint32_t)pc.extent.y }));
}

void JpegRenderer::execute_blocks(RenderGraphContext& ctx, Chain& chain)
{
    PROFILE("JpegRenderer::blocks");

    JpegPushConstants pc = make_push_constants(ctx.render_graph, chain);
    const glm::ivec2 mcu = mcu_size(chain.settings.chroma);
    const glm::ivec2 grid = (glm::ivec2(pc.extent.x, pc.extent.y) + max_shift - 1 + mcu - 1) / mcu;

    ctx.bind_pipeline(blocks_pipeline);
    ctx.bind(resource->query_single(chain.blocks_instance));
    ctx.bind(blocks_resource->query_single(chain.block_list));

    // the codec of the last frame read the list (indirect and in the shader)
    ctx.backend.cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::draw_to_compute);
    pc.codec.w = 0;
    ctx.push_constants(pc);
    ctx.compute({ 1, 1, 1 });

    ctx.backend.cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::compute_to_compute);
    pc.codec.w = 1;
    ctx.push_constants(pc);
    ctx.compute({ ((uint32_t)(grid.x * grid.y) + blocks_group - 1) / blocks_group, 1, 1 });

    // the dispatch counts and the list -> the indirect dispatch of the codec
    ctx.backend.cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::compute_to_draw);
}

void JpegRenderer::execute_codec(RenderGraphContext& ctx, Chain& chain)
{
    PROFILE("JpegRenderer::codec");

    JpegPushConstants pc = make_push_constants(ctx.render_graph, chain);
    const glm::ivec2 mcu = mcu_size(chain.settings.chroma);
    const RBImageHandle work = ctx.render_graph.get_image(chain.work);

    // unmasked chains do not read the list, the layout has it anyway
    const auto block_list = blocks_resource->query_single(chain.block_list);
    ctx.bind_pipeline(codec_pipeline);
    ctx.bind(resource->query_single(chain.codec_instance));
    ctx.bind(block_list);
    for (int generation = 0; generation < chain.settings.generations; ++generation)
    {
        // the generations rewrite the work texture in place: each waits for the previous one
        if (generation > 0)
        {
            ctx.backend.transition_image(ctx.cmd, {
                .debug_pass_name = ctx.render_graph.get_current_pass().name,
                .image = work,
                .src_usage = RBImageUsageType::StorageImage,
                .dst_usage = RBImageUsageType::StorageImage,
                .pass_type = RenderPassType::compute,
            });
        }

        const glm::ivec2 shift = jpeg::grid_shift(chain.settings.seed, generation);
        pc.codec = glm::ivec4(chain.settings.quality, shift.x, shift.y, generation);
        ctx.push_constants(pc);
        if (chain.desc.mask)
        {
            ctx.compute_indirect(block_list->get_ssbo_handle("u_jpeg_blocks", std::nullopt));
            continue;
        }
        // MCUs covering the work area with the grid moved by shift
        ctx.compute({
            (uint32_t)((pc.extent.x + shift.x + mcu.x - 1) / mcu.x),
            (uint32_t)((pc.extent.y + shift.y + mcu.y - 1) / mcu.y),
            1 });
    }
}

void JpegRenderer::execute_output(RenderGraphContext& ctx, Chain& chain)
{
    PROFILE("JpegRenderer::output");

    const JpegPushConstants pc = make_push_constants(ctx.render_graph, chain);
    ctx.bind_pipeline(output_pipeline);
    ctx.bind(resource->query_single(chain.output_instance));
    ctx.push_constants(pc);
    ctx.compute(groups_for({ (uint32_t)pc.extent.z, (uint32_t)pc.extent.w }));
}

void JpegRenderer::add_hit_mask_pass(RenderGraph& graph, RGTextureHandle mask, RGTextureHandle depth, RGTextureHandle geometry_normal,
    RenderResource* camera, RenderResource* gbuffer, std::function<bool()> condition)
{
    checkf(!hit_mask_instance.has_value(), "JpegRenderer::add_hit_mask_pass: one hit mask per renderer");
    hit_mask_instance = next_instance++;
    hit_mask_camera = camera;
    hit_mask_gbuffer = gbuffer;

    graph.add_pass({
        .name = "JpegHitMask",
        .condition = std::move(condition),
        .reads = {
            { depth, RBImageUsageType::Sampled },
            { geometry_normal, RBImageUsageType::Sampled },
        },
        .writes = {
            { mask, RBImageUsageType::StorageImage }
        },
        .execute = [this, mask] (RenderGraphContext& ctx)
        {
            PROFILE("JpegRenderer::hit_mask");
            ctx.bind_pipeline(hit_mask_pipeline);
            ctx.bind(hit_mask_camera, hit_mask_gbuffer);
            ctx.bind(resource->query_single(*hit_mask_instance));
            ctx.compute(groups_for(ctx.render_graph.textures[mask.id].desc.extent));
        },
        .type = RenderPassType::compute
    });
}

void JpegRenderer::prepare_hit_mask(RenderGraphContext& ctx, RBImageHandle mask, const JpegHitsUBO& ubo)
{
    checkf(hit_mask_instance.has_value(), "JpegRenderer::add_hit_mask_pass was not called");
    resource->update_image("u_jpeg_mask", mask, { .frame = ctx.frame }, *hit_mask_instance);
    resource->update_uniform_buffer("jpeg_hits_ubo", ubo, ctx.frame, *hit_mask_instance);
}

void JpegRenderer::prepare_composite(RenderGraphContext& ctx, RBImageHandle scene, RBImageHandle result,
    RBImageHandle hit_result, RBImageHandle hit_mask, RBImageHandle target)
{
    if (!composite_instance)
        composite_instance = next_instance++;

    const UpdateImageParams params{ .frame = ctx.frame };
    resource->update_image("u_jpeg_source", scene, params, *composite_instance);
    resource->update_image("u_jpeg_result", result, params, *composite_instance);
    resource->update_image("u_jpeg_hit_result", hit_result, params, *composite_instance);
    resource->update_image("u_jpeg_mask_sampled", hit_mask, params, *composite_instance);
    resource->update_image("u_jpeg_target", target, params, *composite_instance);
}

void JpegRenderer::dispatch_composite(RenderGraphContext& ctx, const JpegCompositeParams& p, Extent extent)
{
    PROFILE("JpegRenderer::composite");
    checkf(composite_instance.has_value(), "JpegRenderer::prepare_composite was not called");

    if (!ctx.bind_pipeline(composite_pipeline))
        return;
    ctx.bind(resource->query_single(*composite_instance));
    ctx.push_constants(JpegPushConstants{
        .post = glm::vec4(0.0f, std::clamp(p.amount, 0.0f, 1.0f), p.hits ? 1.0f : 0.0f, 0.0f),
        // the seed goes through a float: 24 bits stay exact
        .vignette = glm::vec4(p.vignette_radius, std::max(p.vignette_feather, 1e-3f), std::max(p.block, 0.0f),
            (float)(p.seed & 0xffffffu)),
        .tint = glm::vec4(glm::max(p.tint, glm::vec3(0.0f)), p.tint_radius),
    });
    ctx.compute(groups_for(extent));
}
