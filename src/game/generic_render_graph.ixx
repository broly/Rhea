export module game:generic_render_graph;


import render;
import glm;
import name;
import <map>;
import <memory>;
import engine;
import assets;
import rhmath;
#include "object/object_reflection_macro.h"

class GenericRenderGraph : public RenderGraph
{
public:
    GenericRenderGraph();
    
    void init_resources(const std::map<Name, bool>& parameters) override;
    void build_passes(const std::map<Name, bool>& parameters) override;
    
    
    void draw_scene(RenderGraphContext& ctx);
    void draw_scene_shadow(RenderGraphContext& ctx);
    void draw_clouds(RenderGraphContext& ctx, RGTextureHandle depth_texture, RGTextureHandle noise_texture);
    
    
    bool is_debugging() const;
    
    glm::mat4 build_dir_light_vp() const;
    
    virtual CameraUBO make_camera_ubo(RenderGraphContext& ctx, bool zero_pos = false) const;
    
    
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
    
    
    PipelineObject* shadow_debug_pipeline;
    PipelineObject* tonemap_pipeline;
    
    bool use_swapchain_extent;
    
    
    
    
    Engine* engine;
};
REFLECT_OBJECT(GenericRenderGraph, RenderGraph)