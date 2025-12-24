#include "render_graph.h"

#include "backends/vk/vk_render_backend.h"


RGTextureHandle RenderGraph::create_texture(const RGTextureDesc& desc)
{
    RGTextureHandle handle;
    handle.id = uint32_t(textures.size());

    textures.push_back({ desc, {} });
    return handle;
}

RGResourceHandle RenderGraph::create_buffer(const RGBufferDesc&)
{
    RGResourceHandle handle;
    handle.id = static_cast<uint32_t>(resources.size());

    resources.push_back({ RGResourceKind::Buffer });
    return handle;
}

RGPassId RenderGraph::add_pass(RenderGraphPass&& pass)
{
    RGPassId id{ static_cast<uint32_t>(passes.size()) };
    passes.push_back(std::move(pass));
    return id;
}

void RenderGraph::compile(RenderBackend& backend)
{
    for (auto& tex : textures)
    {
        if (tex.desc.external)
            continue;

        tex.image = backend.create_image({
            .width  = tex.desc.width,
            .height = tex.desc.height,
            .format = tex.desc.format,
            .usage  = tex.desc.usage
        });
    }

    execution_order.clear();
    for (uint32_t i = 0; i < passes.size(); ++i)
        execution_order.push_back(i);
}

void RenderGraph::execute(RenderBackend& backend)
{
    RBFrameHandle frame = backend.get_current_frame();
    backend.wait_for_frame(frame);

    RBCommandList cmd = backend.begin_commands(frame);
    RenderGraphContext ctx(backend, cmd, {});

    for (uint32_t pass_index : execution_order)
    {
        auto& pass = passes[pass_index];
        FramebufferDesc fb_desc{};

        for (auto handle : pass.writes)
        {
            const RGTexture& tex = textures[handle.id];
            RBImageHandle image;

            if (tex.desc.external)
            {
                image = backend.get_swapchain_image(frame);
            }
            else
            {
                image = tex.image.value();
            }

            if (tex.desc.usage & RenderTextureUsage::DepthStencil)
                fb_desc.depth_attachment = image;
            else
                fb_desc.color_attachments.push_back(image);

            fb_desc.width  = tex.desc.width  ? tex.desc.width  : backend.get_swapchain_extent().width;
            fb_desc.height = tex.desc.height ? tex.desc.height : backend.get_swapchain_extent().height;
        }

        // access to render pass here
        auto rp = backend.get_or_create_render_pass(fb_desc);
        
        ctx.framebuffer = backend.get_or_create_framebuffer(fb_desc);

        backend.begin_render_pass(cmd, ctx.framebuffer);
        pass.execute(ctx);
        backend.end_render_pass(cmd);
    }

    backend.end_commands(cmd);
    backend.submit_frame(frame, cmd);
    backend.advance_frame();
}