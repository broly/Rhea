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

struct PreparedPassResources
{
    RenderResourceInstance* camera = nullptr;
    RenderResourceInstance* light  = nullptr;
    RenderResourceInstance* shadow = nullptr;
};

struct PreparedCloudsPass
{
    PipelineObject* pipeline = nullptr;
    RenderResourceInstance* camera = nullptr;
    RenderResourceInstance* instance = nullptr;
};


class GenericRenderGraph : public RenderGraph
{
public:
    GenericRenderGraph();
    
    void init_resources(const std::map<Name, bool>& parameters) override;
    void build_passes(const std::map<Name, bool>& parameters) override;
    void prepare_resources(RenderGraphContext& ctx) override;
    
    void prepare_geometry_batches(RenderGraphContext& ctx, Name pass_name);
    void prepare_geometry_resources(RenderGraphContext& ctx, Name pass_name);
    void prepare_shadow_pass(RenderGraphContext& ctx);
    void prepare_clouds_pass(RenderGraphContext& ctx);
    
    
    void draw_scene(RenderGraphContext& ctx);
    void draw_scene_shadow(RenderGraphContext& ctx);
    void draw_clouds(RenderGraphContext& ctx, RGTextureHandle depth_texture, RGTextureHandle noise_texture);
    
    
    bool is_debugging() const;
    
    glm::mat4 build_dir_light_vp() const;
    
    virtual CameraUBO make_camera_ubo(RenderGraphContext& ctx, bool zero_pos = false, uint32_t face_index = 0) const;
    LightUBO build_light_ubo(glm::vec3 camera_position) const;
    
    
    RGTextureHandle shadow_map;
    RGTextureHandle depth_texture;
    RGTextureHandle noise_texture;
    RGTextureHandle hdr_color;
    RGTextureHandle swapchain_color;
    
    RenderResource* camera_resource = nullptr;
    RenderResource* light_resource = nullptr;
    RenderResource* light_resource_shadow = nullptr;
    RenderResource* shadow_resource = nullptr;
    
    Extent resolution;
    Extent swapchain_extent;
    
    TextureDimension capture_dimension;
    uint32_t num_pass_instances;
    bool allow_shadow_debug;
    
    
    
    std::shared_ptr<Material> tonemap_material;
    std::shared_ptr<Material> shadow_debug_material;
    std::shared_ptr<Material> cloud_material;
    
    
    PipelineObject* shadow_debug_pipeline;
    PipelineObject* clouds_pipeline;
    PipelineObject* tonemap_pipeline;
    
    bool use_swapchain_extent;
    
    using PassBatches = std::vector<DrawBatch>;
    std::unordered_map<Name, PassBatches> prepared_batches;

    // pass_name -> [pass_instance_id] -> pipeline -> resources
    using PipelineResourceMap =
        std::unordered_map<PipelineObject*, PreparedPassResources>;

    std::unordered_map<
        Name,
        std::vector<PipelineResourceMap>
    > prepared_pass_resources;
    
    PreparedCloudsPass prepared_clouds;

    
    Engine* engine;
};
REFLECT_OBJECT(GenericRenderGraph, RenderGraph)