export module render:render_graph;

import :render_backend;
import :rg_types;
import <cassert>;
import <functional>;
#include <map>

export struct RenderGraphPass
{
    std::string name;

    PipelineObject* pipeline;
    std::vector<RGImageUse> reads;
    std::vector<RGImageUse> writes;
    std::vector<RenderResource*> resources;
    RBDescriptorSetLayout descriptor_layout{};
    RBDescriptorSet descriptor_set{};

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
        , pipeline(nullptr)
    {}

    RenderBackend& backend;
    RBCommandList  cmd;
    RBFramebufferId framebuffer;
    RenderGraph& render_graph;
    PipelineObject* pipeline;
    RBFrameHandle frame;
    
};



export class RenderGraph
{
public:
    RenderGraph(const std::shared_ptr<RenderBackend>& in_backend);
    
    
    
    RGTextureHandle create_texture(const RGTextureDesc& desc);
    RGResourceHandle create_buffer(const RGBufferDesc& desc);

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

    RenderResource* create_resource(const RenderResourceDesc& desc);

private:
    struct Resource
    {
        RGResourceType type;
    };

    std::map<PipelineObject*, GraphicsPipelineDesc> pipelines_descs;
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
