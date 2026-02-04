module render:render_graph;

import enum_helpers;
import :render_backend;
import <vulkan/vulkan_core.h>;
import <cassert>;
import profile;
#include "profiling/profile.h"

#include "common/assertion_macros.h"


RBImageHandle RGTexture::get_image(RenderBackend& backend, RBFrameHandle frame) const
{
    return is_swapchain() ? backend.get_swapchain_image(frame) : *image;
}

void RGTexture::memory_barrier(RBCommandList cmd, RenderBackend& backend, RBImageLayout next, RBFrameHandle frame)
{
    checkf(next != RBImageLayout::undefined, "wrong state");
    RBImageHandle img_handle = desc.swapchain_image ? backend.get_swapchain_image(frame) : *image;
    
    auto cur_layout = current_layout ? *current_layout : RBImageLayout::undefined;
    backend.transition_image(cmd, img_handle, cur_layout, next);
    current_layout = next;
}

void RGTexture::reset_layout()
{
    current_layout = RBImageLayout::undefined;
}


void RenderGraph::setup(const std::shared_ptr<RenderBackend>& in_backend, const std::shared_ptr<Renderer>& in_renderer)
{
    backend = in_backend;
    renderer = in_renderer;
}

RGTextureHandle RenderGraph::create_texture(const RGTextureDesc& desc)
{
    RGTextureHandle handle;
    handle.id = uint32_t(textures.size());
    handle.name = desc.name;

    textures.push_back({ desc, {} });
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
    
    RBImageHandle image = backend->create_texture_2d(
        data, TextureCreationInfo {
            TextureFormat::RGBA8,
            generate_mips,
            true,
            RBImageLayout::shader_read_only_optimal,
            RBImageLayout::shader_read_only_optimal,
        }
    );

    textures.push_back({ desc, image });
    return handle;
}

void RenderGraph::set_post_render(std::function<void(RenderGraphContext&)> in_post_render)
{
    post_render = in_post_render;
}

RGPassId RenderGraph::add_pass(RenderGraphPass&& pass)
{
    assert(!graph_compiled);
    
    RGPassId id{ static_cast<uint32_t>(passes.size()) };
    passes.push_back(std::move(pass));
    return id;
}

static RBImageLayout initial_layout_from_usage(RBImageUsage usage)
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
    case RBImageUsage::TransferSrc:
        return RBImageLayout::transfer_src_optimal;

    case RBImageUsage::Present:
        return RBImageLayout::transfer_present;

    default:
        return RBImageLayout::undefined;
    }
}

static RBImageLayout final_layout_from_usage(RBImageUsage usage)
{
    return initial_layout_from_usage(usage);
}



void RenderGraph::compile()
{
    assert(!graph_compiled);

    // Create images
    for (auto& tex : textures)
    {
        if (tex.is_swapchain())
        {
            checkf(tex.desc.usage != RenderTextureUsage::Sampled, "Swapchain texture could not be used as shader parameter");
        }

        if (tex.should_create_image())
        {
            RBImageDesc desc{
                .name = std::string("RG_") + tex.desc.name.to_string(),
                .extent  = tex.desc.extent,
                .format = tex.desc.format,
                .usage  = tex.desc.usage
            };
            tex.image = backend->create_image(desc);
        }
    }
    
    for (auto& pass : passes)
    {
        // validation
        for (auto& read : pass.reads)
        {
            static std::set<RBImageUsage> allowed_in_read = {
                RBImageUsage::SampledFragment,
                RBImageUsage::SampledVertex,
                RBImageUsage::DepthStencilReadOnly,
                RBImageUsage::TransferSrc,
            };
            
            checkf(allowed_in_read.contains(read.usage), 
                "prohibited read usage: %s", reflect::enum_name(read.usage).to_string().c_str());
            
            for (auto& write : pass.writes)
            {
                if (read.texture.id == write.texture.id)
                {
                    const bool is_read_attachment = read.usage == RBImageUsage::ColorAttachment || read.usage == RBImageUsage::DepthStencilAttachment;
                    const bool is_write_sampled = write.usage == RBImageUsage::SampledFragment || write.usage == RBImageUsage::SampledVertex;
                    checkf(!(is_read_attachment && is_write_sampled),
                        "Texture %s could not be used as attachment and read from shader", read.texture.name.to_string().c_str());
                }
            }
        }
        
        for (auto& write : pass.writes)
        {
            static std::set<RBImageUsage> allowed_in_write = {
                RBImageUsage::ColorAttachment,
                RBImageUsage::DepthStencilAttachment,
                RBImageUsage::Present,
            };
            
            checkf(allowed_in_write.contains(write.usage), 
                "prohibited write usage: %s", reflect::enum_name(write.usage).to_string().c_str());
            
            checkf(!textures[write.texture.id].desc.imported,
                "imported textures could not be written");
        }
        
        // generate pass barriers
        for (auto& read : pass.reads)
        {
            auto& texture = textures[read.texture.id];
            
            if (read.usage == RBImageUsage::ColorAttachment || read.usage == RBImageUsage::DepthStencilAttachment)
                continue;

            RBImageLayout before_layout = texture.is_imported()
                                              ? RBImageLayout::shader_read_only_optimal
                                              : initial_layout_from_usage(read.usage);
            
            pass.pass_barriers[read.texture].before_pass = BarrierInfo{
                .layout = before_layout,
            };
        }
        for (auto& write : pass.writes)
        {
            pass.pass_barriers[write.texture].before_pass = BarrierInfo{
                .layout = initial_layout_from_usage(write.usage),
            };
        }
    }
    
    for (size_t pass_index = 0; pass_index < passes.size(); ++pass_index)
    {
        auto& pass = passes[pass_index];

        for (auto& write : pass.writes)
        {
            auto next_usage = find_next_usage(passes, pass_index, write.texture);

            if (!next_usage.has_value())
                continue;

            RBImageLayout after_layout = final_layout_from_usage(*next_usage);
            RBImageLayout current_layout = initial_layout_from_usage(write.usage);

            if (current_layout == after_layout)
                continue;

            pass.pass_barriers[write.texture].after_pass = BarrierInfo{
                .layout = after_layout
            };
        }
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

void RenderGraph::execute(RBCommandList cmd, RBFrameHandle frame, const RenderGraphParameters& params)
{
    PROFILE();
    assert(graph_compiled);

    RenderGraphContext ctx(*backend, cmd, {}, *this, params);
    ctx.frame = frame;

    // Initialize external images (swapchain)
    for (auto& tex : textures)
        tex.reset_layout();  // for swapchain: transfer_present

    for (uint32_t pass_index : execution_order)
    {
        const RenderGraphPass& pass = passes[pass_index];
        
        if (pass.condition && !pass.condition())
            continue;

        // Build framebuffer
        FramebufferDesc fb_desc{};
        fb_desc.pass = pass.name;

        bool size_set = false;

        auto set_size = [&](Extent extent)
        {
            if (!size_set)
            {
                fb_desc.extent = extent;
                size_set = true;
            }
            else
            {
                assert(fb_desc.extent == extent);
            }
        };

        for (const auto& write : pass.writes)
        {
            if (!is_attachment(write.usage))
                continue;

            const RGTexture& tex = textures[write.texture.id];

            RBImageHandle image = tex.get_image(*backend, frame);

            set_size(tex.desc.extent);

            if (tex.desc.usage & RenderTextureUsage::DepthStencil)
            {
                fb_desc.depth_attachment = { image, write.load_op, write.store_op, write.usage };
            }
            else
            {
                fb_desc.color_attachments.push_back({ image, write.load_op, RBStoreOp::Store, write.usage });
            }
        }

        ctx.framebuffer = backend->get_or_create_framebuffer(fb_desc);
        
        for (const auto& [tex, barrier] : pass.pass_barriers)
        {
            auto& texture = textures[tex.id];
            if (barrier.before_pass && texture.allows_barrier())
                texture.memory_barrier(cmd, *backend, barrier.before_pass->layout, frame);
        }

        backend->begin_render_pass(cmd, ctx.framebuffer);

        {
            PROFILE("RenderGraph Pass");
            ctx.pass_name = pass.name;
            pass.execute(ctx);
        }

        backend->end_render_pass(cmd);
        
        for (auto& [tex, barrier] : pass.pass_barriers)
        {
            if (barrier.after_pass)
                textures[tex.id].memory_barrier(cmd, *backend, barrier.after_pass->layout, frame);
        }
    }

    if (auto tex = get_swapchain_texture())
        tex->memory_barrier(cmd, *backend, RBImageLayout::transfer_present, frame);
    
    if (post_render)
    {
        ctx.pass_name = "";
        post_render(ctx);
    }
}


void RenderGraph::rebuild_resources()
{
    auto extent = backend->get_swapchain_extent();

    for (auto& tex : textures)
    {
        if (tex.is_swapchain())
        {
            tex.desc.extent = extent;
            continue;
        }
        
        RBImageDesc desc = {
            .name = std::string("RG_") + tex.desc.name.to_string(),
            .extent  = tex.desc.extent,
            .format = tex.desc.format,
            .usage  = tex.desc.usage
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
            backend->destroy_image(*tex.image, true);
    }
    compile();
}

PipelineObject* RenderGraph::request_pipeline(
    PipelineFamily& pipeline_family, ShaderKey shader_key)
{
    assert(!graph_compiled);
    PipelineObject* pipeline = pipeline_family.request_pipeline(shader_key);
    return pipeline;
}

RGTexture* RenderGraph::get_swapchain_texture()
{
    for (RGTexture& tex : textures)
        if (tex.is_swapchain())
            return &tex;
    return nullptr;
}

void RenderGraph::set_flag(Name name, bool value, bool needs_rebuild)
{
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
