export module render:render_graph;

import :render_backend;
import :rg_types;
import <cassert>;
import <functional>;


export struct RenderGraphPass
{
    std::string name;

    std::vector<RGTextureHandle> reads;
    std::vector<RGTextureHandle> writes;
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
    {}
    
    
    void bind_sampled_texture(
        RBDescriptorSetLayout layout,
        uint32_t binding,
        RGTextureHandle tex);

    RenderBackend& backend;
    RBCommandList  cmd;
    RBFramebufferId framebuffer;
    RenderGraph& render_graph;
    
};



export class RenderGraph
{
public:
    
    RGTextureHandle create_texture(const RGTextureDesc& desc);
    RGResourceHandle create_buffer(const RGBufferDesc& desc);

    RGPassId add_pass(RenderGraphPass&& pass);

    void compile(RenderBackend& backend);
    void execute(RenderBackend& backend, RBCommandList cmd, RBFrameHandle frame);
    
    
    RBImageHandle get_image(RGTextureHandle tex) const
    {
        assert(tex.id < textures.size());
        const auto& rg_tex = textures[tex.id];

        assert(rg_tex.image.has_value());
        return rg_tex.image.value();
    }
    

private:
    struct Resource
    {
        RGResourceType type;
    };

    std::vector<RGResource> resources;
    std::vector<RenderGraphPass> passes;

    std::vector<uint32_t> execution_order;
    
    std::vector<RGTexture> textures;
};
