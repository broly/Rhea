export module game:generic_render_graph;


import render;
import glm;
import name;
import <map>;
import <memory>;
import engine;
import assets;
import rhmath;
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

enum COLOR_OUTPUT : uint8_t
{
    COLOR_OUTPUT_HDR_BASE = 0,
    COLOR_OUTPUT_HDR_RTXGI = 1,
    COLOR_OUTPUT_HDR_SSR = 2,
    COLOR_OUTPUT_HDR_INTERMEDIATE = 3,
    COLOR_OUTPUT_HDR_RESERVED_0 = 4,
    COLOR_OUTPUT_HDR_RTXGI_REPROJECTED = 5,
    COLOR_OUTPUT_HDR_RTXGI_ACCUM = 6,
    COLOR_OUTPUT_HDR_RTXGI_MOMENTS = 7,
    COLOR_OUTPUT_HDR_RTXGI_FILTERED = 8,
    COLOR_OUTPUT_HDR_RESERVED_1 = 9,
    COLOR_OUTPUT_HDR_RESERVED_2 = 10,
    
};
REFLECT_ENUM(COLOR_OUTPUT,
    COLOR_OUTPUT_HDR_BASE,
    COLOR_OUTPUT_HDR_RTXGI,
    COLOR_OUTPUT_HDR_SSR,
    COLOR_OUTPUT_HDR_INTERMEDIATE,
    COLOR_OUTPUT_HDR_RESERVED_0,
    COLOR_OUTPUT_HDR_RTXGI_REPROJECTED,
    COLOR_OUTPUT_HDR_RTXGI_ACCUM,
    COLOR_OUTPUT_HDR_RTXGI_MOMENTS,
    COLOR_OUTPUT_HDR_RTXGI_FILTERED,
    COLOR_OUTPUT_HDR_RESERVED_1,
    COLOR_OUTPUT_HDR_RESERVED_2);

enum GBUFFER_SLOTS
{
    GBUFFER_SLOT_NORMAL = 0,
    GBUFFER_SLOT_WORLD_NORMAL = 1,
    GBUFFER_SLOT_DEPTH = 2,  // special
    GBUFFER_SLOT_LINEAR_DEPTH = 3,
    GBUFFER_SLOT_ALBEDO_ROUGHNESS = 4,
    GBUFFER_SLOT_POSITION = 5,
    GBUFFER_SLOT_MOTION_VECTORS = 6,
    GBUFFER_SLOT_GEOMETRY_NORMAL = 7,
    GBUFFER_SLOT_EMISSIVE = 8,
};
REFLECT_ENUM(GBUFFER_SLOTS,
    GBUFFER_SLOT_NORMAL,
    GBUFFER_SLOT_WORLD_NORMAL,
    GBUFFER_SLOT_DEPTH,
    GBUFFER_SLOT_LINEAR_DEPTH,
    GBUFFER_SLOT_ALBEDO_ROUGHNESS,
    GBUFFER_SLOT_POSITION,
    GBUFFER_SLOT_MOTION_VECTORS,
    GBUFFER_SLOT_GEOMETRY_NORMAL,
    GBUFFER_SLOT_EMISSIVE)


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
    
    std::vector<RGTextureHandle> hdr_color_present;
    std::vector<RGTextureHandle> hdr_color_history;
    
    std::vector<RGTextureHandle> gbuffer;
    std::vector<RGTextureHandle> gbuffer_hist;
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
    
};
REFLECT_OBJECT_FIELDS(GenericRenderGraph, RenderGraph,
    readback_nn)