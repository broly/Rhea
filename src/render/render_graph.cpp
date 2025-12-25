#include "render_graph.h"

#include "backends/vk/vk_render_backend.h"


void RenderGraphContext::bind_sampled_texture(RBDescriptorSetLayout layout, uint32_t binding, RGTextureHandle tex)
{
    backend.update_sampled_image(
        layout,
        binding,
        render_graph.get_image(tex),
        ResourceUsageType::Frame
    );
}

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
    
    for (auto& pass : passes)
    {
        if (!pass.descriptor_set)
            continue;

        for (auto tex_handle : pass.reads)
        {
            const RGTexture& tex = textures[tex_handle.id];

            if (!(tex.desc.usage & RenderTextureUsage::DepthStencil))
                continue;

            backend.update_depth_descriptor(
                pass.descriptor_set,
                tex.image.value(),
                tex.desc.format
            );
        }
    }

    execution_order.clear();
    for (uint32_t i = 0; i < passes.size(); ++i)
        execution_order.push_back(i);
    
}

void RenderGraph::execute(RenderBackend& backend, RBCommandList cmd, RBFrameHandle frame)
{
    RenderGraphContext ctx(backend, cmd, {}, *this);

    for (uint32_t pass_index : execution_order)
    {
        auto& pass = passes[pass_index];
        FramebufferDesc fb_desc{};

        for (auto tex_handle : pass.reads)
        {
            const RGTexture& tex = textures[tex_handle.id];
            if (!tex.image.has_value()) continue;

            backend.CRUTCH_transition_image(
                cmd,
                tex.image.value(),
                tex.desc.format,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }

        // framebuffer build
        for (auto handle : pass.writes)
        {
            const RGTexture& tex = textures[handle.id];

            RBImageHandle image =
                tex.desc.external
                    ? backend.get_swapchain_image()
                    : tex.image.value();

            if (tex.desc.usage & RenderTextureUsage::DepthStencil)
                fb_desc.depth_attachment = image;
            else
                fb_desc.color_attachments.push_back(image);
        }

        ctx.framebuffer = backend.get_or_create_framebuffer(fb_desc);

        backend.begin_render_pass(cmd, ctx.framebuffer);
        pass.execute(ctx);
        backend.end_render_pass(cmd);
    }

    backend.CRUTCH_transition_image(
        cmd,
        backend.get_swapchain_image(),
        RGTextureFormat::RGBA8_UNORM,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );
}
