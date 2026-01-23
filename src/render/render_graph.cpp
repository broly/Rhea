module render:render_graph;

import enum_helpers;
import :render_backend;
import <vulkan/vulkan_core.h>;
import <cassert>;
import profile;
#include "profiling/profile.h"

#include "common/assertion_macros.h"


RenderGraph::RenderGraph(const std::shared_ptr<RenderBackend>& in_backend)
    : backend(in_backend)
{
}

RGTextureHandle RenderGraph::create_texture(const RGTextureDesc& desc)
{
    RGTextureHandle handle;
    handle.id = uint32_t(textures.size());
    handle.name = desc.name;

    textures.push_back({ desc, {} });
    return handle;
}

RGPassId RenderGraph::add_pass(RenderGraphPass&& pass)
{
    assert(!graph_compiled);
    
    RGPassId id{ static_cast<uint32_t>(passes.size()) };
    passes.push_back(std::move(pass));
    return id;
}


static RBImageLayout layout_from_usage(RBImageUsage usage)
{
    switch (usage)
    {
    case RBImageUsage::ColorAttachment:
        return RBImageLayout::color_attachment_optimal;

    case RBImageUsage::DepthStencilAttachment:
        return RBImageLayout::depth_stencil_attachment_optimal;

    case RBImageUsage::DepthStencilReadOnly:
        return RBImageLayout::depth_stencil_read_only_optimal;

    case RBImageUsage::SampledFragment:
        return RBImageLayout::shader_read_only_optimal;

    case RBImageUsage::TransferDst:
        return RBImageLayout::transfer_dst_optimal;

    case RBImageUsage::Present:
        return RBImageLayout::transfer_present;

    default:
        return RBImageLayout::undefined;
    }
}


void RenderGraph::compile()
{
    assert(!graph_compiled);

    // 1. create images
    for (auto& tex : textures)
    {
        tex.current_layout = RBImageLayout::undefined;

        if (tex.desc.external)
            continue;

        RBImageDesc desc{
            .width  = tex.desc.width,
            .height = tex.desc.height,
            .format = tex.desc.format,
            .usage  = tex.desc.usage
        };

        tex.image = backend->create_image(desc);
    }

    // 2. build barriers
    pass_barriers.clear();
    pass_barriers.resize(passes.size());

    for (size_t pass_index = 0; pass_index < passes.size(); ++pass_index)
    {
        auto& pass = passes[pass_index];
        auto& barriers = pass_barriers[pass_index];

        auto process = [&](RGImageUse access)
        {
            RGTexture& tex = textures[access.texture.id];

            if (tex.desc.external)
                return;

            RBImageLayout required =
                layout_from_usage(access.usage);

            if (tex.current_layout != required)
            {
                barriers.push_back({
                    access.texture,
                    tex.current_layout,
                    required
                });

                tex.current_layout = required;
            }
        };

        for (auto& r : pass.reads)  process(r);
        for (auto& w : pass.writes) process(w);
    }

    // linear execution
    execution_order.resize(passes.size());
    std::iota(execution_order.begin(), execution_order.end(), 0);

    graph_compiled = true;
}



bool is_attachment(RBImageUsage usage)
{
    switch (usage)
    {
    case RBImageUsage::ColorAttachment:
    case RBImageUsage::DepthStencilAttachment:
    case RBImageUsage::DepthStencilReadOnly:
        return true;
    default:
        return false;
    }
}

void RenderGraph::execute(RBCommandList cmd, RBFrameHandle frame)
{
    PROFILE();
    assert(graph_compiled);

    RenderGraphContext ctx(*backend, cmd, {}, *this);
    ctx.frame = frame;

    /* --------------------------------------------------------------------
       1. Initialize external image layouts (swapchain)
       -------------------------------------------------------------------- */

    for (auto& tex : textures)
    {
        if (tex.desc.external)
        {
            // Swapchain images are acquired in PRESENT layout
            tex.current_layout = RBImageLayout::transfer_present;
        }
    }

    /* --------------------------------------------------------------------
       2. Execute passes in order
       -------------------------------------------------------------------- */

    for (uint32_t pass_index : execution_order)
    {
        const RenderGraphPass& pass = passes[pass_index];

        /* ------------------------------------------------------------
           2.1 Apply layout transitions required by this pass
           ------------------------------------------------------------ */

        for (const auto& barrier : pass_barriers[pass_index])
        {
            RGTexture& tex = textures[barrier.texture.id];

            RBImageHandle image =
                tex.desc.external
                    ? backend->get_swapchain_image(frame)
                    : tex.image.value();

            backend->transition_image(
                cmd,
                image,
                barrier.before,
                barrier.after
            );

            tex.current_layout = barrier.after;
        }

        /* ------------------------------------------------------------
           2.2 Build framebuffer description
           ------------------------------------------------------------ */

        FramebufferDesc fb_desc{};
        fb_desc.pass = pass.name;

        // Attachments written by this pass
        for (const auto& write : pass.writes)
        {
            if (!is_attachment(write.usage))
                continue;

            const RGTexture& tex = textures[write.texture.id];

            RBImageHandle image =
                tex.desc.external
                    ? backend->get_swapchain_image(frame)
                    : tex.image.value();

            if (tex.desc.usage & RenderTextureUsage::DepthStencil)
            {
                fb_desc.depth_attachment = {
                    image,
                    write.load_op,
                    write.store_op,
                    write.usage
                };
            }
            else
            {
                fb_desc.color_attachments.push_back({
                    image,
                    write.load_op,
                    RBStoreOp::Store,
                    write.usage
                });
            }
        }

        // NOTE:
        // Reads are NOT attachments.
        // Sampled images are bound via descriptors only.
        // Depth preservation is controlled by LOAD_OP_LOAD.

        /* ------------------------------------------------------------
           2.3 Begin render pass
           ------------------------------------------------------------ */

        ctx.framebuffer = backend->get_or_create_framebuffer(fb_desc);

        backend->begin_render_pass(cmd, ctx.framebuffer);

        {
            PROFILE("RenderGraph Pass");
            ctx.pass_name = pass.name;
            pass.execute(ctx);
        }

        backend->end_render_pass(cmd);
    }

    /* --------------------------------------------------------------------
       3. Transition external images back to PRESENT
       -------------------------------------------------------------------- */

    for (auto& tex : textures)
    {
        if (!tex.desc.external)
            continue;

        if (tex.current_layout != RBImageLayout::transfer_present)
        {
            backend->transition_image(
                cmd,
                backend->get_swapchain_image(frame),
                tex.current_layout,
                RBImageLayout::transfer_present
            );

            tex.current_layout = RBImageLayout::transfer_present;
        }
    }
}


RBImageHandle RenderGraph::resolve_image(const RGTexture& tex, std::optional<RBFrameHandle> frame)
{
    if (tex.desc.external)
        return backend->get_swapchain_image(frame);
    else 
        return tex.image.value();
}

void RenderGraph::rebuild_resources()
{
    auto extent = backend->get_swapchain_extent();

    for (auto& tex : textures)
    {
        if (tex.desc.external)
        {
            tex.desc.width  = extent.width;
            tex.desc.height = extent.height;
            continue;
        }
        if (tex.desc.external)
            continue;
        
        RBImageDesc desc = {
            .width  = tex.desc.width,
            .height = tex.desc.height,
            .format = tex.desc.format,
            .usage  = tex.desc.usage
        };
        tex.image = backend->create_image(desc);
    }
}

PipelineObject* RenderGraph::create_pipeline(const GraphicsPipelineDesc& desc)
{
    assert(!graph_compiled);
    PipelineObject* pipeline = backend->create_pipeline(desc);
    pipelines.push_back(pipeline);
    return pipeline;
}

PipelineObject* RenderGraph::request_pipeline(
    PipelineFamily& pipeline_family, ShaderKey shader_key, const PipelineLayoutDesc& layout)
{
    assert(!graph_compiled);
    PipelineObject* pipeline = pipeline_family.request_pipeline(shader_key, layout);
    pipelines.push_back(pipeline);
    return pipeline;
}

RenderResource* RenderGraph::create_resource(const RenderResourceDesc& desc)
{
    assert(!graph_compiled);
    RenderResource* resource = backend->create_resource(desc);
    resources.push_back(resource);
    return resource;
}

void RenderGraph::transition_if_needed(RBCommandList cmd, RGTexture& tex, RBImageUsage next_usage) const
{
    RBImageLayout new_layout = layout_for(next_usage);

    if (tex.current_layout == new_layout)
        return;

    backend->transition_image(
        cmd,
        tex.image.value(),
        tex.current_layout,
        new_layout
    );

    tex.current_layout = new_layout;
}
