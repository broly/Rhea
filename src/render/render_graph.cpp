module render:render_graph;

import enum_helpers;
import :render_backend;
import <vulkan/vulkan_core.h>;
import <cassert>;
import profile;
import gpu_profile;
import :renderer;
import dump_exr;
import paths;
#include "profiling/profile.h"

#include "common/assertion_macros.h"


static RBImageLayout initial_layout_from_usage(RBImageUsageType usage)
{
    
    switch (usage)
    {
    case RBImageUsageType::ColorAttachment:
        return RBImageLayout::color_attachment_optimal;

    case RBImageUsageType::DepthStencilAttachment:
        return RBImageLayout::depth_stencil_attachment_optimal;

        // Depth read-only attachments are NOT used anymore
        // Depth read-only is a pipeline state
    case RBImageUsageType::Sampled:
    case RBImageUsageType::SampledFragment:
        return RBImageLayout::shader_read_only_optimal;

    case RBImageUsageType::StorageImage:
        return RBImageLayout::general;
        
    case RBImageUsageType::TransferDst:
        return RBImageLayout::transfer_dst_optimal;
    case RBImageUsageType::TransferSrc:
        return RBImageLayout::transfer_src_optimal;

    case RBImageUsageType::Present:
        return RBImageLayout::transfer_present;

    default:
        return RBImageLayout::undefined;
    }
}

RGTexture::RGTexture(const RGTextureDesc& in_desc)
    : desc(in_desc)
{
    checkf(desc.num_frames <= MAX_ALLOWED_FRAMES_IN_FLIGHT, "Provided num frames in flight is too high");

    if (desc.dimension == TextureDimension::Cube)
        checkf(desc.num_layers == 6, "Cubemap texture array layers should be always 6");
    

    for (uint32_t f = 0; f < MAX_ALLOWED_FRAMES_IN_FLIGHT; ++f)
    {
        current_usage[f].resize(desc.num_layers);
        // DEPRECATED:
        // current_layouts[f].resize(desc.num_layers);

        for (uint32_t l = 0; l < desc.num_layers; ++l)
        {
            current_usage[f][l].resize(desc.num_mip_levels, RBImageUsageType::Undefined);
        }
        
        // DEPRECATED:
        // for (uint32_t l = 0; l < desc.num_layers; ++l)
        // {
        //     current_layouts[f][l].resize(desc.num_mip_levels, RBImageLayout::undefined);
        // }
    }
}

RBImageHandle RGTexture::get_image(RenderBackend& backend, RBFrameHandle frame) const
{
    return is_swapchain() ? backend.get_swapchain_image(frame) : *image;
}

RBImageView RGTexture::get_image_view(RenderBackend& backend, RBFrameHandle frame, uint32_t layer_index, uint32_t mip_index) const
{
    if (is_swapchain())
        checkf(layer_index == 0 && mip_index == 0, "Unable to get layers from swapchain");
    return backend.get_image_view(get_image(backend, frame), layer_index, mip_index);
}

RBImageUsageType RGTexture::get_usage(uint32_t frame, uint32_t array_index, uint32_t mip_index) const
{
    return current_usage[frame][array_index][mip_index];
}

void RGTexture::memory_barrier(
    RBCommandList cmd,
    RenderBackend& backend,
    Name debug_pass_name,
    RenderPassType pass_type,
    RBImageUsageType src_usage,
    RBImageUsageType dst_usage,
    RBFrameHandle frame,
    uint32_t layer = 0,
    uint32_t mip = 0)
{
    RBImageHandle img_handle =
        desc.swapchain_image
        ? backend.get_swapchain_image(frame)
        : *image;

    RBImageUsageType& current_usage_ref = current_usage[frame][layer][mip];

    // ----------------------------------------------------------------
    // NOTE: do NOT early-out here on `current_usage_ref == dst_usage`.
    //
    // The decision "does this resource need a barrier between pass A and
    // pass B" is made once, in RenderGraph::compile(), and recorded into
    // pass_barriers. If we landed here, compile() asked us to emit a
    // barrier — that decision already accounts for hazards that don't
    // change layout (StorageImage->StorageImage, TransferDst->TransferDst,
    // etc.), and it MUST NOT be silently overridden by a per-frame state
    // check that doesn't differentiate read-vs-write.
    //
    // The previous code skipped the barrier on the second/third/Nth time
    // a resource was used with the same usage across frames — which
    // dropped the WAW barrier on copy-pass history textures (e.g.
    // RG_g_world_normal_hist, RG_hdr_color_history[RTXGI_MOMENTS])
    // and produced WRITE_AFTER_WRITE validation errors that referenced
    // "two copies from the same command buffer".
    //
    // transition_image() itself has a narrower, correct early-out that
    // fires only for pure read->same-pure-read; let it own that decision.
    // ----------------------------------------------------------------

    ImageBarrierParams params{};
    params.image = img_handle;
    params.debug_pass_name = debug_pass_name;

    params.src_usage = src_usage;
    params.dst_usage = dst_usage;
    params.pass_type = pass_type;

    params.base_layer = layer;
    params.base_mip   = mip;
    params.layer_count = 1;
    params.mip_count   = 1;

    backend.transition_image(cmd, params);

    current_usage_ref = dst_usage;
}

void RGTexture::reset_layout()
{
    for (uint8_t frame = 0; frame < desc.num_frames; frame++)
    {
        for (uint32_t layer = 0; layer < desc.num_layers; ++layer)
        {
            for (uint32_t mip = 0; mip < desc.num_mip_levels; ++mip)
            {
                if (is_swapchain())
                {
                    // Right after vkAcquireNextImageKHR, the swapchain image
                    // is physically in VK_IMAGE_LAYOUT_PRESENT_SRC_KHR (it
                    // was put there by the previous frame's explicit
                    // end-of-frame Present transition; see RenderGraph::
                    // execute()). Reflecting that here ensures the per-frame
                    // bookkeeping in RGTexture matches reality on the GPU,
                    // and that compile()'s assumption (last_writer = Present
                    // for swapchain at frame start) lines up with what the
                    // first emitted barrier expects to see.
                    //
                    // Setting Undefined here was the historical bug that
                    // forced "Undefined -> ColorAttachment" barriers, with
                    // srcStage = TOP_OF_PIPE, which Vulkan validation flags
                    // as not synchronizing with vkAcquireNextImageKHR's
                    // implicit read.
                    current_usage[frame][layer][mip] = RBImageUsageType::Present;
                }
                else
                {
                    // For non-swapchain textures the state persists across
                    // frames (and is updated by transition_image on each
                    // barrier), so we leave it alone.
                }
            }
        }
    }
}

void RenderGraphContext::compute(const ComputeWorkgroups& workgroups) const
{
    checkf(render_graph.get_current_pass().type == RenderPassType::compute,
        "could not dispatch in non-compute pass");
    backend.compute(cmd, workgroups);
}

void RenderGraphContext::trace_rays(PipelineObject* pipeline, const Extent& extent, uint32_t depth) const
{
    checkf(render_graph.get_current_pass().type == RenderPassType::rtx,
        "could not trace rays in non-raytrace pass");
    backend.trace_rays(
        cmd,
        pipeline,
        extent,
        depth
    );
}

void RenderGraphContext::copy_img(const CopyImageParams& params) const
{
    backend.copy_image(cmd, params);
}

void RenderGraph::setup(const std::shared_ptr<RenderBackend>& in_backend, const std::shared_ptr<Renderer>& in_renderer)
{
    backend = in_backend;
    renderer = in_renderer;

    num_frames_in_flight = backend->get_num_images_in_flight();
}

RGTextureHandle RenderGraph::create_texture(const RGTextureDesc& desc)
{
    RGTextureHandle handle;
    handle.id = uint32_t(textures.size());
    handle.name = desc.name;
    
    RGTexture tex {desc};
    if (desc.optional_image.has_value())
        tex.image = desc.optional_image;

    textures.push_back(std::move(tex));
    return handle;
}

RGTextureHandle RenderGraph::create_texture_from_asset(TextureHandle tex_handle, bool generate_mips)
{
    RGTextureHandle handle;
    
    const Texture& data = tex_handle.get();
    
    
    handle.id = uint32_t(textures.size());
    handle.name = data.name;
    
    RGTextureDesc desc;
    desc.external = false;
    desc.imported = true;
    desc.format = data.format;
    desc.extent = data.extent;
    desc.name = data.name;
    desc.usage = RenderTextureUsage::Sampled | RenderTextureUsage::TransferDst;
    desc.num_frames = 1;

    const RBImageHandle image = renderer->create_texture_from_asset(tex_handle, generate_mips,
        RBImageLayout::shader_read_only_optimal,
        RBImageLayout::shader_read_only_optimal);

    RGTexture tex {desc};
    tex.image = image;
    
    textures.push_back(std::move(tex));
    return handle;
}

std::vector<RGTextureHandle> RenderGraph::create_textures(const RGTextureDesc& in_desc, uint16_t num_textures, const std::vector<Name>& tex_names)
{
    RGTextureDesc desc = in_desc;
    
    std::string desc_name = desc.name.to_string();
    
    std::vector<RGTextureHandle> result;
    for (uint16_t index = 0; index < num_textures; index++)
    {
        if (tex_names.size() > index)
            desc.name = desc_name + "[" + tex_names[index].to_string() + "]";
        else
            desc.name = Name(desc_name + "[" + std::to_string(index) + "]");
        RGTextureHandle texture = create_texture(desc);
        result.push_back(texture);
    }
    return result;
}

std::vector<RGTextureHandle> RenderGraph::create_textures(const std::vector<RGTextureDesc>& descs)
{
    std::vector<RGTextureHandle> result;
    for (const auto& desc : descs)
    {
        RGTextureHandle texture = create_texture(desc);
        result.push_back(texture);
    }
    return result;
}

RGTextureHandle RenderGraph::duplicate_texture(RGTextureHandle in_texture_handle, Name name)
{
    auto& current_tex = textures[in_texture_handle.id];

    RGTextureDesc desc = current_tex.desc;
    desc.name = name;
    
    RGTextureHandle handle;
    handle.id = uint32_t(textures.size());
    handle.name = desc.name;
    
    RGTexture tex {desc};

    textures.push_back(std::move(tex));
    return handle;
}


RGPassId RenderGraph::add_pass(RenderGraphPass&& pass)
{
    assert(!graph_compiled);
    if (!pass.enabled)
        return RGPassId(-1);
    RGPassId id{ static_cast<uint32_t>(passes.size()) };
    passes.push_back(std::move(pass));
    return id;
}

void RenderGraph::add_exr_dump_pass(const ExrDumpPassDesc& desc)
{
    std::vector<RBImageUsage> reads;
    {
        std::set<uint32_t> seen_texture_ids;
        for (const auto& e : desc.entries)
            if (seen_texture_ids.insert(e.texture.id).second)
                reads.push_back({ e.texture, RBImageUsageType::TransferSrc });
    }

    auto default_condition = [](const RenderGraphParameters& params) -> bool
    {
        return params.render_iter_id == params.num_runs - 1;
    };

    const auto condition_fn = desc.condition ? desc.condition : default_condition;

    std::vector<ExrDumpEntry> entries = desc.entries;
    std::filesystem::path     subdir  = desc.subdir;

    add_pass({
        .name    = desc.name,
        .reads   = std::move(reads),
        .writes  = {},
        .execute = [this, entries = std::move(entries), subdir = std::move(subdir), condition_fn]
                   (RenderGraphContext& ctx)
        {
            if (!condition_fn(ctx.params)) return;

            const std::filesystem::path out_dir = paths::get_cache_path() / subdir;
            std::error_code ec;
            std::filesystem::create_directories(out_dir, ec);

            for (const auto& e : entries)
            {
                RBImageHandle img_handle = get_image(e.texture);
                PendingReadbackHandle readback_handle =
                    ctx.backend.enqueue_image_readback(ctx.cmd, img_handle);

                std::filesystem::create_directories(out_dir/ e.subdir, ec);

                const std::string fname =
                    e.filename_prefix + "_" + std::to_string(ctx.params.output_frame_id) + ".exr";

                pending_exr_saves.push_back({
                    .readback  = readback_handle,
                    .full_path = out_dir / e.subdir / fname,
                    .entry     = e,
                });
            }
        },
        .num_layers = 1,
        .type  = RenderPassType::transfer
    });
}

const RenderGraphPass& RenderGraph::get_current_pass() const
{
    for (auto& pass : passes)
    {
        if (pass.name == current_pass_name)
        {
            return pass;
        }
    }
    unreachable("No such pass");
}


static RBImageLayout final_layout_from_usage(RBImageUsageType usage)
{
    return initial_layout_from_usage(usage);
}

// =============================================================================
// Hazard analysis primitives
// =============================================================================
//
// Two usages are "compatible without sync" iff both are pure reads of the same
// kind: that's the only case where back-to-back access needs no barrier.
// Everything else (any write on either side) requires an execution+memory
// dependency, even if the layout doesn't change.
//
// In particular this catches:
//   - storage->storage (RAW / WAW / WAR with no layout change)
//   - transfer_dst->transfer_dst (WAW)
//   - color_attachment->color_attachment when pass changes (WAW)
//
// A pure read->read on the same usage may still need a barrier if the consumer
// stage differs (e.g. SampledFragment -> SampledVertex), but that is handled
// by the layout/usage equality check below: same RBImageUsageType implies same
// shader stage in our enum, so we can elide.

static bool is_write_usage(RBImageUsageType u)
{
    switch (u)
    {
    case RBImageUsageType::ColorAttachment:
    case RBImageUsageType::DepthStencilAttachment:
    case RBImageUsageType::StorageImage:        // can be read AND written
    case RBImageUsageType::TransferDst:
        return true;
    default:
        return false;
    }
}

// Returns true if a barrier (execution + memory dependency) is required to go
// from `prev` to `next`. We always insert a barrier when either side may write.
static bool needs_barrier(RBImageUsageType prev, RBImageUsageType next)
{
    // Different layouts always require a barrier.
    if (prev != next)
        return true;

    // Same usage type. Read-only -> read-only is fine.
    // Anything where either side may write needs a barrier (RAW/WAW/WAR).
    if (is_write_usage(prev) || is_write_usage(next))
        return true;

    return false;
}

void RenderGraph::compile()
{
    assert(!graph_compiled);

    // =========================
    // CREATE IMAGES
    // =========================
    for (auto& tex : textures)
    {
        if (tex.is_swapchain())
        {
            checkf(tex.desc.usage != RenderTextureUsage::Sampled,
                "Swapchain texture could not be sampled");
        }

        if (tex.should_create_image())
        {
            RBImageDesc desc{
                .name = std::string("RG_") + tex.desc.name.to_string(),
                .extent  = tex.desc.extent,
                .format = tex.desc.format,
                .usage  = tex.desc.usage,
                .mip_levels = tex.desc.num_mip_levels,
                .num_layers = tex.desc.num_layers,
                .is_cubemap = tex.desc.dimension == TextureDimension::Cube,
            };

            tex.image = backend->create_image(desc);
        }
    }

    // =========================
    // INITIAL STATE
    // =========================
    //
    // We keep a two-tier state for every texture:
    //
    //   last_writer       — usage of the most recent write. Layout/access for
    //                       the "src" half of any barrier are derived from it.
    //                       This is what a future writer (WAW) syncs against.
    //
    //   pending_reads     — set of read-usages issued AFTER last_writer with
    //                       no intervening write. A future writer (WAW after
    //                       reads) must sync with all of these (RAW reverse
    //                       a.k.a. WAR), and a future reader of a different
    //                       kind may need a layout transition relative to one
    //                       of them.
    //
    // For "source" of a barrier we always use last_writer when the dst is a
    // write (because writers must wait for both the prior writer AND any
    // pending readers; the layout transition itself rides on the writer's
    // memory). When the dst is a read we go from last_writer if pending_reads
    // is empty, else from the latest pending read with a compatible layout
    // (read->read with same usage is a no-op).
    //
    // Important subtle point: when last_writer != dst usage we MUST also
    // ensure pending readers have completed. The current barrier framework
    // emits a single barrier whose srcStage/srcAccess come from last_writer
    // — which is the strongest dependency since the writer logically
    // happens-before any of the readers, AND any pending reader's writer is
    // the same last_writer. So one barrier sourced from last_writer is
    // sufficient (we synchronize against the writer; readers had their own
    // barriers when they started).
    //
    // The only remaining hazard is WAR with no layout change, e.g.:
    //
    //   PASS A: read tex as SampledFragment
    //   PASS B: write tex as SampledFragment ...  (impossible: not a write usage)
    //
    //   PASS A: read tex as StorageImage  (storage IS a write usage in our enum)
    //   PASS B: write tex as StorageImage  -> caught by needs_barrier(Storage,Storage)=true
    //
    // So the invariant holds: needs_barrier() catches the cases that matter.

    struct ResourceState
    {
        RBImageUsageType last_writer = RBImageUsageType::Undefined;
        // Set of distinct read usages issued since last_writer.
        // We don't need order, only presence — a follow-up write barriers
        // off last_writer (which is "before" all readers in graph order).
        std::vector<RBImageUsageType> pending_reads;

        bool has_pending_reads() const { return !pending_reads.empty(); }

        void record_read(RBImageUsageType u)
        {
            for (auto x : pending_reads)
                if (x == u) return;
            pending_reads.push_back(u);
        }

        void record_write(RBImageUsageType u)
        {
            last_writer = u;
            pending_reads.clear();
        }
    };

    std::vector<ResourceState> state(textures.size());

    for (uint32_t i = 0; i < textures.size(); i++)
    {
        auto& tex = textures[i];

        if (tex.is_swapchain())
        {
            // Imported as Present (last "writer" is the presentation engine /
            // previous frame). We need a barrier on first use anyway because
            // Present->ColorAttachment is a layout change.
            state[i].last_writer = RBImageUsageType::Present;
        }
        else if (tex.is_imported())
        {
            // Treat external/imported textures as already in shader-readable
            // state; first read needs no transition.
            state[i].last_writer = RBImageUsageType::Sampled;
        }
        else
        {
            state[i].last_writer = RBImageUsageType::Undefined;
        }
    }

    // For barrier "src" selection. If we have pending reads and want to
    // consume the resource as a writer, the source of the dependency is the
    // most-recently-recorded reader (because that reader is the latest event
    // we need to wait on). Layout-wise that reader's layout is what the
    // image is currently in — but for read->read with same usage we already
    // skipped emission. For all other cases src layout/usage must be the
    // ACTUAL last access. So:
    auto current_usage_for_src = [](const ResourceState& s) -> RBImageUsageType
    {
        if (s.has_pending_reads())
            return s.pending_reads.back();
        return s.last_writer;
    };

    // =========================
    // BUILD BARRIERS
    // =========================
    for (size_t pass_index = 0; pass_index < passes.size(); ++pass_index)
    {
        auto& pass = passes[pass_index];

        pass.pass_barriers.clear();

        // ---------------------------------------------------------------
        // (1) READS — barrier from current state to read usage
        // ---------------------------------------------------------------
        for (auto& read : pass.reads)
        {
            const uint32_t id = read.texture.id;
            ResourceState& s = state[id];

            const RBImageUsageType prev = current_usage_for_src(s);
            const RBImageUsageType next = read.usage_type;

            if (needs_barrier(prev, next))
            {
                pass.pass_barriers[read.texture].before_pass = BarrierInfo{
                    .src_usage = prev,
                    .dst_usage = next,
                    .is_transition = true
                };
            }

            s.record_read(next);
        }

        // ---------------------------------------------------------------
        // (2) WRITES — barrier from current state to write usage
        //
        // For a write that follows pending reads, src is the writer
        // (last_writer), since the writer logically happens-before all
        // readers and a single barrier sourced from the writer's stage
        // strictly dominates all reader stages: that's wrong in general
        // but for our enum readers only ever read (no write side-effects),
        // so we instead source from the latest reader to ensure we wait
        // for its consumption. needs_barrier() correctly detects WAR on
        // storage-storage and WAW on transfer-transfer.
        // ---------------------------------------------------------------
        for (auto& write : pass.writes)
        {
            const uint32_t id = write.texture.id;
            ResourceState& s = state[id];

            // For writes we want to wait for BOTH the previous writer
            // (WAW) and any pending readers (WAR). When pending_reads is
            // non-empty, the latest reader is the only event later than
            // last_writer — synchronizing against it transitively covers
            // last_writer (since last_writer happens-before reader).
            // For layout transitions the "src layout" must be whatever the
            // image is actually in right now, which is the latest access.
            const RBImageUsageType prev = current_usage_for_src(s);
            const RBImageUsageType next = write.usage_type;

            if (needs_barrier(prev, next))
            {
                pass.pass_barriers[write.texture].before_pass = BarrierInfo{
                    .src_usage = prev,
                    .dst_usage = next,
                    .is_transition = true
                };
            }

            s.record_write(next);
        }

        // ---------------------------------------------------------------
        // (3) AFTER PASS (LOOKAHEAD)
        //
        // For each texture we wrote, peek at the next pass that touches
        // it. If the next consumer expects a different usage and needs a
        // barrier, we request one. This complements (2) at the next pass
        // and is robust against passes being conditionally skipped at
        // execute-time (the after_pass barrier of the prior pass still
        // runs). Worst case it's a duplicate, in which case the runtime
        // skips it because usages already match.
        // ---------------------------------------------------------------
        for (auto& write : pass.writes)
        {
            const uint32_t id = write.texture.id;

            std::optional<RBImageUsageType> next_usage;

            for (size_t next_pass = pass_index + 1; next_pass < passes.size(); ++next_pass)
            {
                auto& p = passes[next_pass];

                for (auto& r : p.reads)
                {
                    if (r.texture.id == id) { next_usage = r.usage_type; break; }
                }
                if (!next_usage)
                {
                    for (auto& w : p.writes)
                    {
                        if (w.texture.id == id) { next_usage = w.usage_type; break; }
                    }
                }
                if (next_usage) break;
            }

            if (!next_usage)
                continue;

            const RBImageUsageType current = write.usage_type;

            if (needs_barrier(current, *next_usage))
            {
                pass.pass_barriers[write.texture].after_pass = BarrierInfo{
                    .src_usage = current,
                    .dst_usage = *next_usage,
                    .is_transition = true
                };
            }
        }
    }

    execution_order.resize(passes.size());
    std::iota(execution_order.begin(), execution_order.end(), 0);

    graph_compiled = true;
}

bool is_attachment(RBImageUsageType usage)
{
    switch (usage)
    {
    case RBImageUsageType::ColorAttachment:
    case RBImageUsageType::DepthStencilAttachment:
    case RBImageUsageType::DepthStencilReadOnly:
        return true;
    default:
        return false;
    }
}


void RenderGraph::flush_pending_exr_saves()
{
    if (pending_exr_saves.empty()) return;

    for (auto& job : pending_exr_saves)
    {
        ImageReadback readback = backend->finalize_readback(job.readback);

        const auto& e = job.entry;
        checkf(e.layer < readback.layers, "...");
        checkf(e.mip   < readback.mips,   "...");

        const std::vector<float>& pixels = readback.data[e.layer][e.mip];
        const Extent mip_extent = ImageReadback::mip_extent(readback.extent, e.mip);

        const uint32_t src_channels = readback.channels;
        const uint32_t out_channels = (e.out_channels == 0) ? src_channels : e.out_channels;

        exr_dump::save_exr_padded(
            job.full_path, pixels.data(),
            mip_extent.width, mip_extent.height,
            src_channels, out_channels, e.placeholder);
    }
    pending_exr_saves.clear();
}

void RenderGraph::execute(RBCommandList cmd, RBFrameHandle frame, const RenderGraphParameters& params, RGPostRenderCallback callback)
{
    PROFILE("RenderGraph::execute");
    assert(graph_compiled);

    // GPU pass profiler: open this frame's timestamp pool (drains the results
    // recorded MAX_FRAMES_IN_FLIGHT frames ago, then resets for fresh recording).
    gpuprof::frame_begin(frame, cmd);

    RenderGraphContext ctx(*backend, cmd, {}, *this, params);
    ctx.frame = frame;

    // Initialize external images (swapchain)
    for (auto& tex : textures)
        tex.reset_layout();  // for swapchain: transfer_present
    
    ctx.is_preparing = true;
    prepare_resources(ctx);
    ctx.is_preparing = false;

    for (uint32_t pass_index : execution_order)
    {
        const RenderGraphPass& pass = passes[pass_index];
        
        if (pass.condition && !pass.condition())
            continue;
        
        for (const auto& [tex, barrier] : pass.pass_barriers)
        {
            
            auto& texture = textures[tex.id];

            if (!texture.allows_barrier(ctx.frame))
                continue;

            if (barrier.before_pass)
            {
                const auto& b = *barrier.before_pass;

                for (uint32_t layer = 0; layer < texture.get_layers_count(); layer++)
                {
                    texture.memory_barrier(
                        cmd,
                        *backend,
                        pass.name,
                        pass.type,
                        b.src_usage,
                        b.dst_usage,
                        frame,
                        layer
                    );
                }
            }
        }
        
        for (uint32_t layer_id = 0; layer_id < pass.num_layers; ++layer_id)
        {
            for (uint32_t mip_map_id = 0; mip_map_id < pass.num_mip_maps; ++mip_map_id)
            {
                // Build framebuffer
                FramebufferDesc fb_desc{};
                fb_desc.pass = pass.name;



                if (pass.type == RenderPassType::graphics)
                {
                    
                    for (const auto& write : pass.writes)
                    {
                        if (!is_attachment(write.usage_type))
                            continue;

                        const RGTexture& tex = textures[write.texture.id];

                        RBImageHandle image = tex.get_image(*backend, frame);

                        if (!fb_desc.is_extent_set())
                            fb_desc.update_extent(tex.desc.extent);
                    
                        const bool is_depth_attachment = tex.desc.usage & RenderTextureUsage::DepthStencil;
                    
                        AttachmentDesc attachment_desc {
                            .image_name = tex.desc.name,
                            .image = image,
                            .load = write.load_op,
                            .store = write.store_op,
                            .usage = write.usage_type,
                            .layer = layer_id,
                            .mip_level = mip_map_id,
                            .depth_attachment = is_depth_attachment
                        };
                    
                        fb_desc.attachments.push_back(attachment_desc);

                    }
                    
                    ctx.framebuffer = backend->get_or_create_framebuffer(fb_desc);
                }
                else
                {
                    ctx.framebuffer = {};
                }
                current_pass_name = pass.name;
                
                if (pass.type == RenderPassType::graphics)
                    backend->begin_render_pass(cmd, ctx.framebuffer);

                {
                    PROFILE("RenderGraph Pass");
                    gpuprof::pass_begin(cmd, pass.name.to_string());
                    ctx.pass_name = pass.name;
                    ctx.level = layer_id;
                    ctx.mip = mip_map_id;
                    pass.execute(ctx);
                    gpuprof::pass_end(cmd);
                }

                if (pass.type == RenderPassType::graphics)
                    backend->end_render_pass(cmd);
        
        
            }
        
        }
        
        
        for (auto& [tex, barrier] : pass.pass_barriers)
        {
            if (!barrier.after_pass)
                continue;

            auto& texture = textures[tex.id];
            const auto& b = *barrier.after_pass;

            if (pass.name == "GeometryTranslucent" && tex.name == "g_depth")
            {
                __nop();
            }
            
            for (uint32_t layer = 0; layer < texture.get_layers_count(); layer++)
            {
                texture.memory_barrier(
                    cmd,
                    *backend,
                    pass.name,
                    pass.type,
                    b.src_usage, 
                    b.dst_usage,
                    frame,
                    layer
                );
            }
        }
    }
    
    if (callback)
    {
        ctx.pass_name = "";
        callback(ctx);
    }

    // ---------------------------------------------------------------
    // Explicit end-of-frame Present transition for swapchain images.
    //
    // We used to rely on the render-pass attribute finalLayout=PRESENT_SRC_KHR
    // to transition the swapchain image. That works in terms of the actual
    // GPU layout, but it bypasses our ImageResource::state bookkeeping —
    // transition_image only updates state when it is the one emitting the
    // barrier. The next frame would then see stale state (ColorAttachment)
    // and skip the Present->ColorAttachment transition, leaving the image
    // physically in PRESENT_SRC_KHR while the engine assumes it's already
    // in ColorAttachment. Result: VUID-vkCmdDraw-None-09600.
    //
    // Fix: keep render-pass finalLayout aligned with the attachment usage
    // (e.g. ColorAttachment), and emit an explicit transition to Present
    // here. This keeps state and reality in sync.
    for (auto& tex : textures)
    {
        if (!tex.is_swapchain())
            continue;

        // Only transition layer 0 of swapchain (it's not array-typed).
        // src_usage in params is unused by transition_image (it reads the
        // actual current state from the image itself); pass_type is used
        // only for dst stage selection — we want Present's stage anyway.
        ImageBarrierParams params{};
        params.image = tex.get_image(*backend, ctx.frame);
        params.debug_pass_name = Name("EndOfFrameSwapchainPresent");
        params.dst_usage = RBImageUsageType::Present;
        params.pass_type = RenderPassType::present;
        params.base_layer = 0;
        params.base_mip = 0;
        params.layer_count = 1;
        params.mip_count = 1;
        backend->transition_image(cmd, params);

        // Mirror in render-graph bookkeeping so allows_barrier()/etc see it.
        for (uint32_t f = 0; f < MAX_ALLOWED_FRAMES_IN_FLIGHT; ++f)
            if (!tex.current_usage[f].empty() && !tex.current_usage[f][0].empty())
                tex.current_usage[f][0][0] = RBImageUsageType::Present;
    }

    end_frame();

    one_time_render_flags.clear();

    frame_index++;
}


void RenderGraph::rebuild_resources()
{
    backend->destroy_render_pass_cache();
    auto extent = backend->get_swapchain_extent();

    for (auto& tex : textures)
    {
        if (tex.is_swapchain())
        {
            tex.desc.extent = extent;
            continue;
        }
        
        if (tex.is_imported())
        {
            continue;
        }
        
        if (tex.image.has_value())
        {
            backend->destroy_image(*tex.image, true);
            tex.image.reset();
        }
        
        RBImageDesc desc = {
            .name = std::string("RG_") + tex.desc.name.to_string(),
            .extent  = tex.desc.extent,
            .format = tex.desc.format,
            .usage  = tex.desc.usage,
            .mip_levels = tex.get_mip_levels_count(),
            .num_layers = tex.get_layers_count(),
            //.old_image_handle = tex.image,
        };
        tex.image = backend->create_image(desc);
    }
}



void RenderGraph::recompile()
{
    assert(graph_compiled);
    
    graph_compiled = false;
    
    for (auto& pass : passes)
    {
        pass.pass_barriers.clear();
    }
    
    for (auto& tex : textures)
    {
        if (tex.image && tex.should_create_image())
        {
            destroy_texture_image(tex);
        }
    }
    compile();
}

void RenderGraph::destroy_texture_image(RGTexture& tex) const
{
    ensure(tex.image.has_value());
    if (tex.image.has_value())
    {
        destroy_texture_image(tex);
    }
}

PipelineObject* RenderGraph::request_pipeline(
    std::shared_ptr<PipelineFamily> pipeline_family, ShaderKey shader_key)
{
    // assert(!graph_compiled);
    PipelineObject* pipeline = pipeline_family->request_pipeline(shader_key);
    return pipeline;
}

RGTexture* RenderGraph::get_swapchain_texture()
{
    for (RGTexture& tex : textures)
        if (tex.is_swapchain())
            return &tex;
    return nullptr;
}


void RenderGraph::set_flag(Name name, bool value, bool needs_rebuild, bool one_time)
{
    if (one_time)
        one_time_render_flags[name] = value;
    else
        render_flags[name] = value;
}

void RenderGraph::toggle_flag(Name name, bool needs_rebuild)
{
    render_flags[name] = !render_flags[name];
}

bool RenderGraph::get_render_flag(Name name) const
{
    auto render_flag_it = render_flags.find(name);
    if (render_flag_it != render_flags.end())
        return render_flag_it->second;
    return false;
}