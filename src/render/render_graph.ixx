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

export enum class RenderGraphMode
{
    Camera,
    ReflectionCapture,
};

export struct CubemapInfo
{
    glm::vec3 position;
    unsigned int face_index;
};

export struct RenderGraphParameters
{
    RenderGraphMode mode;
    
    std::optional<CubemapInfo> cubemap;
    
    std::map<Name, float> extra;
};

export class RenderGraphContext
{
public:
    RenderGraphContext(
        RenderBackend& in_backend,
        RBCommandList in_cmd,
        RBFramebufferId in_fb, 
        class RenderGraph& render_graph,
        const RenderGraphParameters& in_params = {})
        : backend(in_backend)
        , cmd(in_cmd)
        , framebuffer(in_fb)
        , render_graph(render_graph)
        , params(in_params)
    {}
    
    Name pass_name;
    RenderBackend& backend;
    RBCommandList cmd;
    RBFramebufferId framebuffer;
    RenderGraph& render_graph;
    RBFrameHandle frame;
    RenderGraphParameters params;
};

export class RenderGraph
{
public:
    RenderGraph(const std::shared_ptr<RenderBackend>& in_backend);
    
    
    
    RGTextureHandle create_texture(const RGTextureDesc& desc);
    RGTextureHandle create_texture_from_asset(TextureHandle texture, bool generate_mips = true);

    RGPassId add_pass(RenderGraphPass&& pass);

    void compile();
    void execute(RBCommandList cmd, RBFrameHandle frame, const RenderGraphParameters& params);


    RBImageHandle get_image(RGTextureHandle tex) const
    {
        assert(tex.id < textures.size());
        const auto& rg_tex = textures[tex.id];

        assert(rg_tex.image.has_value());
        return rg_tex.image.value();
    }
    
    void rebuild_resources();
    
    void recompile();

    PipelineObject* request_pipeline(
        PipelineFamily& pipeline_family, ShaderKey shader_key);

private:
    std::vector<RenderGraphPass> passes;

    std::vector<uint32_t> execution_order;
    
    std::vector<RGTexture> textures;
    std::shared_ptr<RenderBackend> backend;
    std::vector<std::vector<RGImageBarrier>> pass_barriers;


    bool graph_compiled = false;
};
