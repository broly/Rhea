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
    Frustum   frustum;
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

    void prepare_geometry_resources(RenderGraphContext& ctx);
    void prepare_shadow_pass(RenderGraphContext& ctx);
    void prepare_clouds_pass(RenderGraphContext& ctx);
    void prepare_wireframe_pass(RenderGraphContext& ctx);
    void prepare_ssr(RenderGraphContext& ctx);
    
    void draw_fullscreen_copy(
        RenderGraphContext& ctx,
        RGTextureHandle source);
    
    
    void draw_scene(RenderGraphContext& ctx);
    void draw_scene_shadow(RenderGraphContext& ctx);
    void draw_clouds(RenderGraphContext& ctx, RGTextureHandle depth_texture, RGTextureHandle noise_texture);
    void draw_wireframe(RenderGraphContext& ctx);
    void draw_ssr(RenderGraphContext& ctx);
    void draw_ssr_composite(RenderGraphContext& ctx);
    void draw_rtxgi(RenderGraphContext& ctx);
    
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
    
    RGTextureHandle hdr_color;
    // RGTextureHandle hdr_color_rtxgi;
    RGTextureHandle hdr_color_temp;
    
    RGTextureHandle g_depth;
    RGTextureHandle g_normal;
    RGTextureHandle g_world_normal;
    RGTextureHandle g_motion_vectors;
    RGTextureHandle g_roughness;
    RGTextureHandle g_albedo;
    RGTextureHandle g_position;
    RGTextureHandle g_shadow;
    
    
    RGTextureHandle history_hdr;
    RGTextureHandle ssr_texture;
    uint32_t history_index = 0;
    
    RGTextureHandle brdf_lut;
    
    RBAccelStruct tlas = {};
    
    RenderResource* camera_resource = nullptr;
    RenderResource* gbuffer_resource = nullptr;
    RenderResource* light_resource = nullptr;
    RenderResource* shadow_resource = nullptr;
    RenderResource* reflection_resource = nullptr;
    RenderResource* copy_resource = nullptr;
    RenderResource* ssr_resource = nullptr;
    RenderResource* hdr_color_resource = nullptr;
    RenderResource* hdr_color_storage_resource = nullptr;
    RenderResource* tlas_resource = nullptr;
    RenderResource* mesh_table_resource = nullptr;
    RenderResource* clouds_resource = nullptr;
    RenderResource* base_color_resource = nullptr;
    RenderResource* pbr_material_ssbo_resource = nullptr;
    RenderResource* textures_resource = nullptr;
    RenderResource* primitive_table_resource = nullptr;
    
    Extent resolution;
    Extent swapchain_extent;
    
    TextureDimension capture_dimension;
    uint32_t num_pass_instances;
    bool allow_shadow_debug;
    
    CameraUBO current_camera_ubo;
        
    
    PipelineObject* copy_pipeline;
    PipelineObject* shadow_debug_pipeline;
    PipelineObject* clouds_pipeline;
    PipelineObject* tonemap_pipeline;
    PipelineObject* wireframe_pipeline;
    PipelineObject* ssr_pipeline;
    PipelineObject* ssr_composite_pipeline;
    PipelineObject* rtx_gi_pipeline;

    
    std::shared_ptr<PipelineFamily> copy_pipeline_family;
    std::shared_ptr<PipelineFamily> tonemap_pipeline_family;
    std::shared_ptr<PipelineFamily> shadow_debug_pipeline_family;
    std::shared_ptr<PipelineFamily> wireframe_pipeline_family;
    std::shared_ptr<PipelineFamily> ssr_pipeline_family;
    std::shared_ptr<PipelineFamily> ssr_composite_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_pipeline_family;
    
    bool use_swapchain_extent;
    
    using PassBatches = std::vector<DrawBatch>;
    std::unordered_map<Name, PassBatches> prepared_batches;
    
    
    
    Engine* engine;
    
    
    RBVertexBufferHandle debug_line_buffer;
    uint8_t*     debug_line_mapped = nullptr;

    uint32_t debug_line_capacity = 19440*32;
    
};
REFLECT_OBJECT(GenericRenderGraph, RenderGraph)