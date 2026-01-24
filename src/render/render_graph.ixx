export module render:render_graph;

import :render_backend;
import :rg_types;
import <cassert>;
import <functional>;
import <map>;
import name;
import :pipeline_family;



export struct RenderGraphPass
{
    Name name;
    
    std::function<bool()> condition = nullptr;
    std::vector<RGImageUse> reads;
    std::vector<RGImageUse> writes;

    std::function<void(class RenderGraphContext&)> execute;
};

export class RenderGraphContext
{
public:
    RenderGraphContext(
        RenderBackend& in_backend,
        RBCommandList in_cmd,
        RBFramebufferId in_fb, 
        class RenderGraph& render_graph)
        : backend(in_backend)
        , cmd(in_cmd)
        , framebuffer(in_fb)
        , render_graph(render_graph)
    {}

    Name pass_name;
    RenderBackend& backend;
    RBCommandList  cmd;
    RBFramebufferId framebuffer;
    RenderGraph& render_graph;
    RBFrameHandle frame;
    
};



export class RenderGraph
{
public:
    RenderGraph(const std::shared_ptr<RenderBackend>& in_backend);
    
    
    
    RGTextureHandle create_texture(const RGTextureDesc& desc);

    RGPassId add_pass(RenderGraphPass&& pass);

    void compile();
    void execute(RBCommandList cmd, RBFrameHandle frame);
    
    RBImageHandle resolve_image(const RGTexture& tex, std::optional<RBFrameHandle> frame = std::nullopt);
    
    
    RBImageHandle get_image(RGTextureHandle tex) const
    {
        assert(tex.id < textures.size());
        const auto& rg_tex = textures[tex.id];

        assert(rg_tex.image.has_value());
        return rg_tex.image.value();
    }
    
    void rebuild_resources();
    
    PipelineObject* create_pipeline(const GraphicsPipelineDesc& desc);
    PipelineObject* request_pipeline(
        PipelineFamily& pipeline_family, ShaderKey shader_key, const PipelineLayoutDesc& layout);

    RenderResource* create_resource(const RenderResourceDesc& desc);
    
    static RBImageLayout layout_for(RBImageUsage usage)
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
        case RBImageUsage::TransferSrc:
            return RBImageLayout::transfer_src_optimal;
        case RBImageUsage::Present:
            return RBImageLayout::transfer_present;
        default:
            return RBImageLayout::undefined;
        }
    }
    
    void transition_if_needed(
        RBCommandList cmd,
        RGTexture& tex,
        RBImageUsage next_usage) const;

private:
    struct Resource
    {
        RGResourceType type;
    };

    std::vector<RGResource> rg_resources;
    std::vector<RenderGraphPass> passes;

    std::vector<uint32_t> execution_order;
    
    std::vector<RGTexture> textures;
    std::shared_ptr<RenderBackend> backend;
    std::vector<std::vector<RGImageBarrier>> pass_barriers;
    
    
    
    std::vector<PipelineObject*> pipelines; 
    std::vector<RenderResource*> resources;
    
    bool graph_compiled = false;
};
