module render:render_graph;

import enum_helpers;
import :render_backend;
import <vulkan/vulkan_core.h>;


void RenderGraphContext::bind_sampled_texture(RBDescriptorSetLayout layout, uint32_t binding, RGTextureHandle tex)
{
    backend.update_sampled_image(
        layout,
        binding,
        render_graph.get_image(tex),
        ResourceUsageType::Frame
    );
}

RenderGraph::RenderGraph(const std::shared_ptr<RenderBackend>& in_backend)
    : backend(in_backend)
{
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

void RenderGraph::compile()
{
    for (auto& tex : textures)
    {
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
    
    for (auto& pass : passes)
    {
        if (!pass.descriptor_set)
            continue;

        for (auto tex_handle : pass.reads)
        {
            const RGTexture& tex = textures[tex_handle.texture.id];

            if (!(tex.desc.usage & RenderTextureUsage::DepthStencil))
                continue; 
            
            auto image = resolve_image(tex);

            backend->update_depth_descriptor(
                pass.descriptor_set,
                image,
                tex.desc.format
            );
        }
    }
    
    
    pass_barriers.clear();
    pass_barriers.resize(passes.size());
    
    for (size_t pass_index = 0; pass_index < passes.size(); ++pass_index)
    {
        auto& pass = passes[pass_index];
        auto& barriers = pass_barriers[pass_index];

        // READS
        for (const auto& read : pass.reads)
        {
            RGTexture& tex = textures[read.texture.id];

            if (tex.current_usage != read.usage)
            {
                barriers.push_back({
                    read.texture,
                    tex.current_usage,
                    read.usage
                });

                tex.current_usage = read.usage;
            }
        }

        // WRITES
        for (const auto& write : pass.writes)
        {
            RGTexture& tex = textures[write.texture.id];

            if (tex.current_usage != write.usage)
            {
                barriers.push_back({
                    write.texture,
                    tex.current_usage,
                    write.usage
                });

                tex.current_usage = write.usage;
            }
        }
    }

    execution_order.clear();
    for (uint32_t i = 0; i < passes.size(); ++i)
        execution_order.push_back(i);
}


void RenderGraph::execute(RBCommandList cmd, RBFrameHandle frame)
{
    RenderGraphContext ctx(*backend, cmd, {}, *this);
    
    for (auto& tex : textures)
    {
        if (tex.desc.external)
            tex.current_usage = RBImageUsage::Undefined;
    }

    for (size_t i = 0; i < execution_order.size(); ++i)
    {
        uint32_t pass_index = execution_order[i];
        auto& pass = passes[pass_index];

        // 1. Apply barriers
        for (const auto& barrier : pass_barriers[pass_index])
        {
            auto tex = textures[barrier.texture.id];
            
            
            RBImageHandle image = resolve_image(tex, frame);
            
            backend->transition_image(
                cmd,
                image,
                barrier.before,
                barrier.after
            );
        }

        // framebuffer build
        FramebufferDesc fb_desc{};
        for (auto handle : pass.writes)
        {
            const RGTexture& tex = textures[handle.texture.id];

            RBImageHandle image =
                tex.desc.external
                    ? backend->get_swapchain_image()
                    : tex.image.value();

            if (tex.desc.usage & RenderTextureUsage::DepthStencil)
                fb_desc.depth_attachment = image;
            else
                fb_desc.color_attachments.push_back(image);
        }

        ctx.framebuffer = backend->get_or_create_framebuffer(fb_desc);

        backend->begin_render_pass(cmd, ctx.framebuffer);
        pass.execute(ctx);
        backend->end_render_pass(cmd);
    }
    
    for (auto& tex : textures)
    {
        if (tex.desc.external &&
            tex.current_usage != RBImageUsage::Present)
        {
            backend->transition_image(
                cmd,
                resolve_image(tex),
                tex.current_usage,
                RBImageUsage::Present
            );

            tex.current_usage = RBImageUsage::Present;
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
    
    for (auto& pass : passes)
    {
        if (!pass.descriptor_set)
            continue;

        for (auto tex_handle : pass.reads)
        {
            const RGTexture& tex = textures[tex_handle.texture.id];

            if (!(tex.desc.usage & RenderTextureUsage::DepthStencil))
                continue;

            backend->update_depth_descriptor(
                pass.descriptor_set,
                tex.image.value(),
                tex.desc.format
            );
        }
    }
}
