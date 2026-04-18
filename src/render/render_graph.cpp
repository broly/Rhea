module render:render_graph;

import enum_helpers;
import :render_backend;
import <vulkan/vulkan_core.h>;
import <cassert>;
import profile;
import :renderer;
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

    // DEPRECATED
    // auto& current_layout = current_layouts[frame][layer][mip];
    RBImageUsageType& current_usage_ref = current_usage[frame][layer][mip];

    
    // DEPRECATED
    // RBImageLayout new_layout = initial_layout_from_usage(dst_usage);

    if (current_usage_ref == dst_usage)
        return;

    ImageBarrierParams params{};
    params.image = img_handle;
    params.debug_pass_name = debug_pass_name;

    // DEPRECATED:
    // params.before = current_layout;
    // params.after  = new_layout;

    params.src_usage = src_usage;
    params.dst_usage = dst_usage;
    params.pass_type = pass_type;

    params.base_layer = layer;
    params.base_mip   = mip;
    params.layer_count = 1;
    params.mip_count   = 1;

    backend.transition_image(cmd, params);

    // DEPRECATED:
    // current_layout = new_layout;
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
                    current_usage[frame][layer][mip] = RBImageUsageType::Undefined;
                    // DEPRECATED:
                    // current_layouts[frame][layer][mip] = RBImageLayout::undefined;
                }
                else
                {
                    // current_usage[frame][layer][mip] = RBImageUsage::Undefined;
                    // current_layouts[frame][layer][mip] = RBImageLayout::undefined;
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
    std::vector<RBImageUsageType> last_usage(textures.size());

    for (uint32_t i = 0; i < textures.size(); i++)
    {
        auto& tex = textures[i];

        if (tex.is_swapchain())
            last_usage[i] = RBImageUsageType::Present;
        else if (tex.is_imported())
            last_usage[i] = RBImageUsageType::Sampled;
        else
            last_usage[i] = RBImageUsageType::Undefined;
    }

    // =========================
    // BUILD BARRIERS
    // =========================
    for (size_t pass_index = 0; pass_index < passes.size(); ++pass_index)
    {
        auto& pass = passes[pass_index];

        pass.pass_barriers.clear();

        // =========================
        // BEFORE PASS (READS)
        // =========================
        for (auto& read : pass.reads)
        {
            uint32_t id = read.texture.id;

            RBImageUsageType prev = last_usage[id];
            RBImageUsageType next = read.usage_type;

            if (prev != next)
            {
                pass.pass_barriers[read.texture].before_pass = BarrierInfo{
                    .src_usage = prev,
                    .dst_usage = next,
                    .is_transition = true
                };
            }
        }
        
        // =========================
        // BEFORE PASS (READ THEN WRITE)
        // =========================
        for (auto& write : pass.writes)
        {
            if (write.load_op == RBLoadOp::Load)
            {
                uint32_t id = write.texture.id;

                RBImageUsageType prev = last_usage[id];
                RBImageUsageType next = write.usage_type;

                {
                    pass.pass_barriers[write.texture].before_pass = BarrierInfo{
                        .src_usage = prev,
                        .dst_usage = next,
                        .is_transition = true
                    };
                }
            }
        }
        
        
        // =========================
        // BEFORE PASS (WRITES)
        // =========================
        for (auto& write : pass.writes)
        {
            uint32_t id = write.texture.id;

            RBImageUsageType prev = last_usage[id];
            RBImageUsageType next = write.usage_type;

            if (prev != next)
            {
                pass.pass_barriers[write.texture].before_pass = BarrierInfo{
                    .src_usage = prev,
                    .dst_usage = next,
                    .is_transition = true
                };
            }

            last_usage[id] = next;
        }

        // =========================
        // AFTER PASS (LOOKAHEAD)
        // =========================
        for (auto& write : pass.writes)
        {
            uint32_t id = write.texture.id;

            std::optional<RBImageUsageType> next_usage;

            for (size_t next_pass = pass_index + 1; next_pass < passes.size(); ++next_pass)
            {
                auto& p = passes[next_pass];

                for (auto& r : p.reads)
                {
                    if (r.texture.id == id)
                    {
                        next_usage = r.usage_type;
                        break;
                    }
                }

                if (!next_usage)
                {
                    for (auto& w : p.writes)
                    {
                        if (w.texture.id == id)
                        {
                            next_usage = w.usage_type;
                            break;
                        }
                    }
                }

                if (next_usage)
                    break;
            }

            if (!next_usage)
                continue;

            RBImageUsageType current = write.usage_type;

            if (current != *next_usage)
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

void RenderGraph::execute(RBCommandList cmd, RBFrameHandle frame, const RenderGraphParameters& params, RGPostRenderCallback callback)
{
    PROFILE("RenderGraph::execute");
    assert(graph_compiled);

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
