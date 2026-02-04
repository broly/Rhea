export module render:render_graph;

import :render_backend;
import :rg_types;
import <cassert>;
import <functional>;
import <map>;
import name;
import :pipeline_family;
#include "object/object_reflection_macro.h"


export struct RenderGraphPass
{
    Name name;
    
    std::function<bool()> condition = nullptr;
    std::vector<RGImageUse> reads;
    std::vector<RGImageUse> writes;

    std::function<void(class RenderGraphContext&)> execute;
    
    std::map<RGTextureHandle, RGImageBarriers> pass_barriers;
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


struct RGTexture
{
    RGTextureDesc desc;
    std::optional<RBImageHandle> image;
        
    std::optional<RBImageLayout> current_layout;
    
    RBImageHandle get_image(RenderBackend& backend, RBFrameHandle frame) const;
    
    
    bool should_create_image() const
    {
        return !desc.swapchain_image && !desc.imported;
    }
    
    bool allows_barrier() const
    {
        if (is_imported())
            return current_layout.has_value();
        return true;
    }
    
    bool is_imported() const
    {
        return desc.imported;
    }

    void memory_barrier(RBCommandList cmd, RenderBackend& backend, RBImageLayout next, RBFrameHandle frame);
    
    bool is_swapchain() const
    {
        return desc.swapchain_image;
    }
    
    void reset_layout();
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

export class RenderGraph : public RhObject
{
public:
    void setup(
        const std::shared_ptr<RenderBackend>& in_backend,
        const std::shared_ptr<Renderer>& in_renderer);
    
    virtual void init_render_graph(const std::map<Name, bool>& parameters) = 0;
    
    RGTextureHandle create_texture(const RGTextureDesc& desc);
    RGTextureHandle create_texture_from_asset(TextureHandle texture, bool generate_mips = true);

    RGPassId add_pass(RenderGraphPass&& pass);

    void compile();
    void execute(RBCommandList cmd, RBFrameHandle frame, const RenderGraphParameters& params);
    
    static std::optional<RBImageUsage> find_next_usage(
        const std::vector<RenderGraphPass>& passes,
        size_t current_pass_index,
        RGTextureHandle tex)
    {
        for (size_t i = current_pass_index + 1; i < passes.size(); ++i)
        {
            const auto& pass = passes[i];

            for (const auto& read : pass.reads)
                if (read.texture.id == tex.id)
                    return read.usage;

            for (const auto& write : pass.writes)
                if (write.texture.id == tex.id)
                    return write.usage;
        }
        return std::nullopt;
    }


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

    std::vector<RenderGraphPass> passes;

    std::vector<uint32_t> execution_order;
    
    std::vector<RGTexture> textures;
    std::shared_ptr<RenderBackend> backend;
    std::shared_ptr<Renderer> renderer;
    
    RGTexture* get_swapchain_texture();
    

    void set_flag(Name name, bool value, bool needs_rebuild = false);
    void toggle_flag(Name name, bool needs_rebuild = false);
    bool get_render_flag(Name name) const;
    

    bool graph_compiled = false;
    
protected: // dynamic info
    std::map<Name, bool> render_flags;
};
REFLECT_OBJECT(RenderGraph, RhObject);