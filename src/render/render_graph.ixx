export module render:render_graph;

import :render_backend;
import :rg_types;
import <cassert>;
import <functional>;
import <map>;
import name;
import :pipeline_family;
import :common;
import :rg_params;
#include "common/assertion_macros.h"
#include "object/object_reflection_macro.h"


constexpr uint8_t MAX_ALLOWED_FRAMES_IN_FLIGHT = 5;

export enum class RenderPassType
{
    graphics,
    compute,
    rtx,
};


export struct RenderGraphPass
{
    Name name;
    
    std::function<bool()> condition = nullptr;
    
    std::vector<RGImageUse> reads;
    std::vector<RGImageUse> writes;

    std::function<void(class RenderGraphContext&)> execute;
    
    std::map<RGTextureHandle, RGImageBarriers> pass_barriers;
    
    uint32_t num_layers = 1;
    uint32_t num_mip_maps = 1;
    
    RenderPassType type = RenderPassType::graphics;
};



struct RGTexture
{
    RGTexture(const RGTextureDesc& in_desc);

    RGTextureDesc desc;
    std::optional<RBImageHandle> image = std::nullopt;
        
    // current_layouts[frame][layer][mip]
    std::array<
        std::vector<std::vector<RBImageLayout>>,
        MAX_ALLOWED_FRAMES_IN_FLIGHT
    > current_layouts;
    
    RBImageHandle get_image(RenderBackend& backend, RBFrameHandle frame) const;
    RBImageView get_image_view(RenderBackend& backend, RBFrameHandle frame, uint32_t array_index = 0, uint32_t mip_index = 0) const;
    
    RBImageLayout get_layout( uint32_t frame, uint32_t array_index = 0, uint32_t mip_index = 0) const;
    
    
    bool should_create_image() const
    {
        return !desc.swapchain_image && !desc.imported;
    }
    
    bool allows_barrier(RBFrameHandle frame) const
    {
        if (is_imported())
            return current_layouts[frame][0][0] != RBImageLayout::undefined;
        return true;
    }
    
    bool is_imported() const
    {
        return desc.imported;
    }

    void memory_barrier(
        RBCommandList cmd,
        RenderBackend& backend,
        RBImageLayout next,
        RBFrameHandle frame,
        uint32_t layer = 0,
        uint32_t mip = 0);
    
    bool is_swapchain() const
    {
        return desc.swapchain_image;
    }
    
    uint32_t get_layers_count() const
    {
        return desc.array_layers;
    }
    
    uint32_t get_mip_levels_count() const
    {
        return desc.mip_levels;
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
    uint32_t level;
    uint32_t mip;
    bool is_preparing;
    
    mutable PipelineObject* current_pipeline = nullptr;
    
    void bind(RenderResource* resource) const
    {
        checkf(current_pipeline, "Could not bind resource while no any pipeline bound");
        resource->query_single(level)->bind(cmd, frame);
    }
    
    template<typename... Ts>
    requires (sizeof...(Ts) > 1)
    void bind(Ts... vs) const
    {
        (bind(vs), ...);
    }
    
    
    void bind(const std::shared_ptr<RenderResourceInstance>& instance) const
    {
        checkf(current_pipeline, "Could not bind resource while no any pipeline bound");
        instance->bind(cmd, frame);
    }
    
    bool bind_pipeline(PipelineObject* pipeline) const
    {
        const bool pipeline_changed = current_pipeline != pipeline;
        current_pipeline = pipeline;
        backend.bind_pipeline(cmd, pipeline);
        
        return pipeline_changed;
    }
    
    template<typename T>
    void push_constants(T&& value) const
    {
        backend.push_constants(cmd, std::forward<T>(value));
    }
    
    void draw_indexed(size_t size) const
    {
        backend.draw_indexed(cmd, size);
    }
    
    void bind_mesh(MeshPrimHandle mesh) const
    {
        backend.bind_mesh(cmd, mesh, frame);
    }
    
    void draw_fullscreen() const
    {
        backend.draw_fullscreen(cmd);
    }
    
    void draw(uint32_t vertex_count = 0) const
    {
        backend.draw(cmd, vertex_count);
    }
};


export class RenderGraph : public RhObject
{
public:
    void setup(
        const std::shared_ptr<RenderBackend>& in_backend,
        const std::shared_ptr<Renderer>& in_renderer);
    
    virtual void init_resources(const std::map<Name, bool>& parameters) = 0;
    virtual void build_passes(const std::map<Name, bool>& parameters) = 0;
    virtual void end_frame() {};
    virtual void prepare_resources(RenderGraphContext& ctx) {};
    
    RGTextureHandle create_texture(const RGTextureDesc& desc);
    RGTextureHandle create_texture_from_asset(TextureHandle texture, bool generate_mips = true);
    
    RGTextureHandle duplicate_texture(RGTextureHandle in_texture_handle, Name name);

    RGPassId add_pass(RenderGraphPass&& pass);

    void compile();
    void execute(RBCommandList cmd, RBFrameHandle frame, const RenderGraphParameters& params, RGPostRenderCallback callback);
    
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

    std::optional<RBImageHandle> get_image_by_name(Name name)
    {
        for (auto& texture : textures)
            if (texture.desc.name == name)
                return texture.image.value();
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
        std::shared_ptr<PipelineFamily> pipeline_family, ShaderKey shader_key);

    std::vector<RenderGraphPass> passes;

    std::vector<uint32_t> execution_order;
    
    std::vector<RGTexture> textures;
    std::shared_ptr<RenderBackend> backend;
    std::shared_ptr<Renderer> renderer;
    
    Name current_pass_name;
    
    RGTexture* get_swapchain_texture();
    

    void set_flag(Name name, bool value, bool needs_rebuild = false);
    void toggle_flag(Name name, bool needs_rebuild = false);
    bool get_render_flag(Name name) const;
    

    uint8_t num_frames_in_flight;
    
    bool graph_compiled = false;
    
protected: // dynamic info
    std::map<Name, bool> render_flags;
};
REFLECT_OBJECT(RenderGraph, RhObject);