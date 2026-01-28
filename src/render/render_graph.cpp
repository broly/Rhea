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

        // Depth read-only attachments are NOT used anymore
        // Depth read-only is a pipeline state

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

    // Create images
    for (auto& tex : textures)
    {
        tex.current_layout = RBImageLayout::undefined;

        if (!tex.desc.external)
        {
            RBImageDesc desc{
                .name = std::string("RG_") + tex.desc.name.to_string(),
                .width  = tex.desc.width,
                .height = tex.desc.height,
                .format = tex.desc.format,
                .usage  = tex.desc.usage
            };
            tex.image = backend->create_image(desc);
        }
    }

    pass_barriers.clear();
    pass_barriers.resize(passes.size());

    for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
    {
        auto& pass = passes[pass_index];
        
        auto& barriers = pass_barriers[pass_index];

        auto process = [&](const RGImageUse& access)
        {
            RGTexture& tex = textures[access.texture.id];

            RBImageLayout required = layout_from_usage(access.usage);

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

        for (const auto& r : pass.reads)  process(r);
        for (const auto& w : pass.writes) process(w);
    }

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

    // Initialize external images (swapchain)
    for (auto& tex : textures)
    {
        if (tex.desc.external)
        {
            tex.current_layout = RBImageLayout::transfer_present;
        }
    }

    for (uint32_t pass_index : execution_order)
    {
        const RenderGraphPass& pass = passes[pass_index];
        
        if (pass.condition && !pass.condition())
            continue;

        // Apply transitions
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

        // Build framebuffer
        FramebufferDesc fb_desc{};
        fb_desc.pass = pass.name;

        bool size_set = false;

        auto set_size = [&](uint32_t w, uint32_t h)
        {
            if (!size_set)
            {
                fb_desc.width  = w;
                fb_desc.height = h;
                size_set = true;
            }
            else
            {
                assert(fb_desc.width == w && fb_desc.height == h);
            }
        };

        for (const auto& write : pass.writes)
        {
            if (!is_attachment(write.usage))
                continue;

            const RGTexture& tex = textures[write.texture.id];

            RBImageHandle image =
                tex.desc.external
                    ? backend->get_swapchain_image(frame)
                    : tex.image.value();

            set_size(tex.desc.width, tex.desc.height);

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

        ctx.framebuffer = backend->get_or_create_framebuffer(fb_desc);

        backend->begin_render_pass(cmd, ctx.framebuffer);

        {
            PROFILE("RenderGraph Pass");
            ctx.pass_name = pass.name;
            pass.execute(ctx);
        }

        backend->end_render_pass(cmd);
    }

    // Transition swapchain back to PRESENT
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
            .name = std::string("RG_") + tex.desc.name.to_string(),
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
    PipelineFamily& pipeline_family, ShaderKey shader_key)
{
    assert(!graph_compiled);
    PipelineObject* pipeline = pipeline_family.request_pipeline(shader_key);
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
