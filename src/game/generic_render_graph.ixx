export module game:generic_render_graph;


import render;
import glm;
import name;
import <map>;
import <memory>;
import engine;
import assets;
import rhmath;
import :nn_denoiser_passes;
import <unordered_map>;
#include "object/object_reflection_macro.h"


struct DrawItem
{
    MeshPrimHandle mesh;
    glm::mat4 world;
    std::shared_ptr<Material> material;
};

struct DrawBatch
{
    PipelineObject* pipeline = nullptr;
    std::vector<DrawItem> items;
};

struct ViewInfo
{
    glm::mat4 view;
    glm::mat4 proj;
    Frustum frustum;
};

enum class COLOR_OUTPUT_HDR : uint8_t
{
    BASE = 0,
    RTXGI = 1,
    SSR = 2,
    INTERMEDIATE = 3,
    RESERVED_0 = 4,
    RTXGI_REPROJECTED = 5,
    RTXGI_ACCUM = 6,
    RTXGI_MOMENTS = 7,
    RTXGI_FILTERED = 8,
    RTXGI_NEURAL_DENOISED = 9,
    RESERVED_2 = 10,
    
};
REFLECT_ENUM(COLOR_OUTPUT_HDR,
    BASE,
    RTXGI,
    SSR,
    INTERMEDIATE,
    RESERVED_0,
    RTXGI_REPROJECTED,
    RTXGI_ACCUM,
    RTXGI_MOMENTS,
    RTXGI_FILTERED,
    RTXGI_NEURAL_DENOISED,
    RESERVED_2);

enum class GBUFFER_SLOTS : uint8_t
{
    NORMAL = 0,
    WORLD_NORMAL = 1,
    DEPTH = 2,  // special
    LINEAR_DEPTH = 3,
    ALBEDO_ROUGHNESS = 4,
    POSITION = 5,
    MOTION_VECTORS = 6,
    GEOMETRY_NORMAL = 7,
    EMISSIVE = 8,
};
REFLECT_ENUM(GBUFFER_SLOTS,
    NORMAL,
    WORLD_NORMAL,
    DEPTH,
    LINEAR_DEPTH,
    ALBEDO_ROUGHNESS,
    POSITION,
    MOTION_VECTORS,
    GEOMETRY_NORMAL,
    EMISSIVE)


struct HDROutputTextureArray : std::vector<RGTextureHandle>
{
    using std::vector<RGTextureHandle>::vector;
    HDROutputTextureArray() {}
    HDROutputTextureArray(const std::vector<RGTextureHandle>& array)
        : std::vector<RGTextureHandle>(array)
    {}
    using std::vector<RGTextureHandle>::operator[];
    RGTextureHandle operator[](COLOR_OUTPUT_HDR enumerator)
    {
        return std::vector<RGTextureHandle>::operator[](static_cast<size_t>(enumerator));
    }
};

struct GBufferArray : std::vector<RGTextureHandle>
{
    using std::vector<RGTextureHandle>::vector;
    GBufferArray() {};
    GBufferArray(const std::vector<RGTextureHandle>& array)
        : std::vector<RGTextureHandle>(array)
    {}
    using std::vector<RGTextureHandle>::operator[];
    RGTextureHandle operator[](GBUFFER_SLOTS enumerator)
    {
        return std::vector<RGTextureHandle>::operator[](static_cast<size_t>(enumerator));
    }
};

class GenericRenderGraph : public RenderGraph
{
public:
    GenericRenderGraph();
    
    void init_resources(const std::map<Name, bool>& parameters) override;
    void build_passes(const std::map<Name, bool>& parameters) override;
    void prepare_resources(RenderGraphContext& ctx) override;
    void prepare_raytracing(RenderGraphContext& ctx);
    void end_frame() override;
    
    void rebuild_camera_ubo(RenderGraphContext& ctx);
    
    virtual void on_pso_built() override;

    void prepare_geometry_resources(RenderGraphContext& ctx);
    void prepare_shadow_pass(RenderGraphContext& ctx);
    void prepare_clouds_pass(RenderGraphContext& ctx);
    void prepare_wireframe_pass(RenderGraphContext& ctx);
    void prepare_ssr(RenderGraphContext& ctx);
    
    void setup_hdr_color_table(RenderGraphContext& ctx) const;


    void draw_scene(RenderGraphContext& ctx);
    void draw_scene_shadow(RenderGraphContext& ctx);
    void draw_clouds(RenderGraphContext& ctx, RGTextureHandle depth_texture, RGTextureHandle noise_texture);
    void draw_wireframe(RenderGraphContext& ctx);
    void draw_ssr(RenderGraphContext& ctx);
    void draw_ssr_composite(RenderGraphContext& ctx);
    void draw_rtxgi(RenderGraphContext& ctx);
    
    void add_copy_pass(Name name, RGTextureHandle src, RGTextureHandle dst, bool ping_pong = true);
    
    void prepare_nn_denoiser_passes();

    ViewInfo  build_view_info(
        RenderGraphContext& ctx,
        bool zero_pos) const;
    
    bool is_debugging() const;
    
    glm::mat4 build_dir_light_vp() const;
    
    virtual CameraUBO make_camera_ubo(RenderGraphContext& ctx, bool zero_pos = false, uint32_t face_index = 0) const;
    LightUBO build_light_ubo(glm::vec3 camera_position) const;
    void bind_shadow_globals(
        RenderGraphContext& ctx);
    
    RGTextureHandle shadow_map;
    RGTextureHandle noise_texture;
    RGTextureHandle swapchain_color;
    
    HDROutputTextureArray hdr_color_present;
    HDROutputTextureArray hdr_color_history;
    
    GBufferArray gbuffer;
    GBufferArray gbuffer_hist;
    RGTextureHandle decal_albedo;
    
    RGTextureHandle ssr_texture;
    uint32_t history_index = 0;
    
    RGTextureHandle brdf_lut;
    
    RBAccelStruct tlas = {};
    
    RenderResource* camera_resource = nullptr;
    RenderResource* gbuffer_resource = nullptr;
    RenderResource* dbuffer_resource = nullptr;
    RenderResource* light_resource = nullptr;
    RenderResource* shadow_resource = nullptr;
    RenderResource* reflection_resource = nullptr;
    RenderResource* ssr_resource = nullptr;
    RenderResource* hdr_color_output_resource = nullptr;
    RenderResource* hdr_color_storage_resource = nullptr;
    RenderResource* tlas_resource = nullptr;
    RenderResource* mesh_table_resource = nullptr;
    RenderResource* clouds_resource = nullptr;
    RenderResource* base_color_resource = nullptr;
    RenderResource* pbr_material_table_resource = nullptr;
    RenderResource* textures_resource = nullptr;
    RenderResource* primitive_table_resource = nullptr;
    
    Extent resolution;
    Extent swapchain_extent;
    
    TextureDimension capture_dimension;
    uint32_t num_pass_instances;
    bool allow_shadow_debug;
    
    CameraUBO current_camera_ubo;
        
    
    PipelineObject* shadow_debug_pipeline;
    PipelineObject* clouds_pipeline;
    PipelineObject* tonemap_pipeline;
    PipelineObject* wireframe_pipeline;
    PipelineObject* ssr_pipeline;
    PipelineObject* ssr_composite_pipeline;
    PipelineObject* rtx_gi_pipeline;
    PipelineObject* rtx_gi_reproject_pipeline;
    PipelineObject* rtx_gi_temporal_accum_pipeline;
    PipelineObject* rtx_gi_moments_pipeline;
    PipelineObject* rtx_gi_spatial_filter_pipeline;
    PipelineObject* lighting_pipeline;

    
    std::shared_ptr<PipelineFamily> tonemap_pipeline_family;
    std::shared_ptr<PipelineFamily> shadow_debug_pipeline_family;
    std::shared_ptr<PipelineFamily> wireframe_pipeline_family;
    std::shared_ptr<PipelineFamily> ssr_pipeline_family;
    std::shared_ptr<PipelineFamily> ssr_composite_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_reproject_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_temporal_accum_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_moments_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_spatial_filter_pipeline_family;
    std::shared_ptr<PipelineFamily> lighting_pipeline_family;
    
    bool use_swapchain_extent;
    
    using PassBatches = std::vector<DrawBatch>;
    std::unordered_map<Name, PassBatches> prepared_batches;
    
    
    
    Engine* engine;
    
    
    RBVertexBufferHandle debug_line_buffer;
    uint8_t*     debug_line_mapped = nullptr;

    uint32_t debug_line_capacity = 19440*32;
    
    bool readback_nn = false;
    
    NNDenoiserState nn_denoiser_state;
    
};
REFLECT_OBJECT_FIELDS(GenericRenderGraph, RenderGraph,
    readback_nn)