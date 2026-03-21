module render:render_graph;

import enum_helpers;
import :render_backend;
import <vulkan/vulkan_core.h>;
import <cassert>;
import profile;
import :renderer;
#include "profiling/profile.h"

#include "common/assertion_macros.h"


RGTexture::RGTexture(const RGTextureDesc& in_desc)
    : desc(in_desc)
{
    checkf(desc.num_frames <= MAX_ALLOWED_FRAMES_IN_FLIGHT, "Provided num frames in flight is too high");

    if (desc.dimension == TextureDimension::Cube)
        checkf(desc.array_layers == 6, "Cubemap texture array layers should be always 6");

    for (uint32_t f = 0; f < MAX_ALLOWED_FRAMES_IN_FLIGHT; ++f)
    {
        current_layouts[f].resize(desc.array_layers);

        for (uint32_t l = 0; l < desc.array_layers; ++l)
        {
            current_layouts[f][l].resize(desc.mip_levels, RBImageLayout::undefined);
        }
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

RBImageLayout RGTexture::get_layout(uint32_t frame, uint32_t array_index, uint32_t mip_index) const
{
    return current_layouts[frame][array_index][mip_index];
}

void RGTexture::memory_barrier(
    RBCommandList cmd,
    RenderBackend& backend,
    RBImageLayout next,
    RBFrameHandle frame,
    uint32_t layer,
    uint32_t mip)
{
    if (is_swapchain())
        return;

    checkf(next != RBImageLayout::undefined, "wrong state");

    RBImageHandle img_handle =
        desc.swapchain_image
        ? backend.get_swapchain_image(frame)
        : *image;

    auto& current_layout = current_layouts[frame][layer][mip];

    if (current_layout == next)
        return;
    
    ImageBarrierParams params;
    params.image = img_handle;
    params.before = current_layout;
    params.after = next;
    params.base_layer = layer;
    params.base_mip = mip;
    params.mip_count = 1;
    params.layer_count = 1;

    backend.transition_image(cmd, params);

    current_layout = next;
}

void RGTexture::reset_layout()
{
    for (uint8_t frame = 0; frame < desc.num_frames; frame++)
    {
        for (uint32_t layer = 0; layer < desc.array_layers; ++layer)
        {
            for (uint32_t mip = 0; mip < desc.mip_levels; ++mip)
            {
                if (is_swapchain())
                    current_layouts[frame][layer][mip] = RBImageLayout::transfer_present;
                else
                    current_layouts[frame][layer][mip] = RBImageLayout::undefined;
            }
        }
    }
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

std::vector<RGTextureHandle> RenderGraph::create_textures(const RGTextureDesc& in_desc, uint16_t num_textures)
{
    RGTextureDesc desc = in_desc;
    
    std::string desc_name = desc.name.to_string();
    
    std::vector<RGTextureHandle> result;
    for (uint16_t index = 0; index < num_textures; index++)
    {
        desc.name = Name(desc_name + "_" + std::to_string(index));
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

    case RBImageUsage::StorageImage:
        return RBImageLayout::general;
        
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
                .usage  = tex.desc.usage,
                .mip_levels = tex.desc.mip_levels,
                .num_layers = tex.desc.array_layers,
                .is_cubemap = tex.desc.dimension == TextureDimension::Cube,
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
                RBImageUsage::StorageImage,
            };
            
            checkf(allowed_in_read.contains(read.usage), 
                "prohibited read usage: %s", reflect::enum_name(read.usage).to_string().c_str());
            
            for (auto& write : pass.writes)
            {
                checkf(read.texture.id != write.texture.id, "Simultaneous writing and reading is prohibited. Check the pass '%s' for texture '%s'",
                    pass.name.to_string().c_str(), textures[read.texture.id].desc.name.to_string().c_str());
                // if (read.texture.id == write.texture.id)
                // {
                //     const bool is_read_attachment = read.usage == RBImageUsage::ColorAttachment || read.usage == RBImageUsage::DepthStencilAttachment;
                //     const bool is_write_sampled = write.usage == RBImageUsage::SampledFragment || write.usage == RBImageUsage::SampledVertex;
                //     checkf(!(is_read_attachment && is_write_sampled),
                //         "Texture %s could not be used as attachment and read from shader", read.texture.name.to_string().c_str());
                // }
            }
        }
        
        for (auto& write : pass.writes)
        {
            static std::set<RBImageUsage> allowed_in_write = {
                RBImageUsage::ColorAttachment,
                RBImageUsage::DepthStencilAttachment,
                RBImageUsage::Present,
                RBImageUsage::StorageImage,
            };
            
            checkf(allowed_in_write.contains(write.usage), 
                "prohibited write usage: %s", reflect::enum_name(write.usage).to_string().c_str());
            
            auto& texture = textures[write.texture.id];
            
            checkf(!texture.desc.imported,
                "imported textures could not be written");
            
            checkf(pass.num_layers <= texture.get_layers_count(), 
                "could not handle pass (%s) with num_instances greater than texture (%s) array count",
                    pass.name.to_string().c_str(), texture.desc.name.to_string().c_str());
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

void RenderGraph::execute(RBCommandList cmd, RBFrameHandle frame, const RenderGraphParameters& params, RGPostRenderCallback callback)
{
    PROFILE("RenderGraph::execute");
    assert(graph_compiled);

    RenderGraphContext ctx(*backend, cmd, {}, *this, params);
    ctx.frame = frame;

    // Initialize external images (swapchain)
    // for (auto& tex : textures)
    //     tex.reset_layout();  // for swapchain: transfer_present
    
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
            if (barrier.before_pass && texture.allows_barrier(ctx.frame))
                for (uint32_t layer = 0; layer < texture.get_layers_count(); layer++)
                    texture.memory_barrier(cmd, *backend, barrier.before_pass->layout, frame, layer);
        }
        
        
        for (uint32_t layer_id = 0; layer_id < pass.num_layers; ++layer_id)
        {
            for (uint32_t mip_map_id = 0; mip_map_id < pass.num_mip_maps; ++mip_map_id)
            {
                // Build framebuffer
                FramebufferDesc fb_desc{};
                fb_desc.pass = pass.name;


                for (const auto& write : pass.writes)
                {
                    if (!is_attachment(write.usage))
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
                        .usage = write.usage,
                        .layer = layer_id,
                        .mip_level = mip_map_id,
                        .depth_attachment = is_depth_attachment
                    };
                    
                    fb_desc.attachments.push_back(attachment_desc);

                }

                ctx.framebuffer = backend->get_or_create_framebuffer(fb_desc);
        
                current_pass_name = pass.name;
                
                if (pass.type == RenderPassType::graphics)
                    backend->begin_render_pass(cmd, ctx.framebuffer);

                {
                    PROFILE("RenderGraph Pass");
                    ctx.pass_name = pass.name;
                    ctx.level = layer_id;
                    ctx.mip = mip_map_id;
                    pass.execute(ctx);
                }

                if (pass.type == RenderPassType::graphics)
                    backend->end_render_pass(cmd);
        
        
            }
        
        }
        
        
        for (auto& [tex, barrier] : pass.pass_barriers)
        {
            if (barrier.after_pass)
                for (uint32_t layer = 0; layer < textures[tex.id].get_layers_count(); layer++)
                    textures[tex.id].memory_barrier(cmd, *backend, barrier.after_pass->layout, frame, layer);
        }
    }

    if (auto tex = get_swapchain_texture())
        tex->memory_barrier(cmd, *backend, RBImageLayout::transfer_present, frame);
    
    if (callback)
    {
        ctx.pass_name = "";
        callback(ctx);
    }
    
    end_frame();
    
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
            backend->destroy_image(*tex.image, true);
    }
    compile();
}

PipelineObject* RenderGraph::request_pipeline(
    std::shared_ptr<PipelineFamily> pipeline_family, ShaderKey shader_key)
{
    assert(!graph_compiled);
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
