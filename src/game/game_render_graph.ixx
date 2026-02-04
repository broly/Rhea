export module game:render_graph;

import render;
import glm;
import name;
import <map>;
import <memory>;
import engine;
import assets;
#include "object/object_reflection_macro.h"


class GameRenderGraph : public RenderGraph
{
public:
    void init_render_graph(const std::map<Name, bool>& parameters) override;
    
    
    bool is_debugging() const;
    
    glm::mat4 build_dir_light_vp() const;
    
    void draw_scene(RenderGraphContext& ctx);
    void draw_scene_shadow(RenderGraphContext& ctx);
    void draw_clouds(RenderGraphContext& ctx, RGTextureHandle depth_texture, RGTextureHandle noise_texture);
    
    CameraUBO make_camera_ubo(RenderGraphContext& ctx, bool zero_pos = false) const;
    
    void pass_shadow_map(RenderGraphContext& ctx);
    void pass_shadow_debug(RenderGraphContext& ctx);
    void pass_geometry_base(RenderGraphContext& ctx);
    void pass_translucent(RenderGraphContext& ctx);
    void pass_clouds(RenderGraphContext& ctx);
    void pass_tonemapping(RenderGraphContext& ctx);
    void pass_readback(RenderGraphContext& ctx);
    
    RGTextureHandle shadow_map;
    
    RenderResource* camera_resource;
    RenderResource* light_resource;
    RenderResource* light_resource_shadow;
    RenderResource* shadow_resource;
    
    RGTextureHandle swapchain_color;
    RGTextureHandle depth_texture;
    RGTextureHandle noise_texture;
    RGTextureHandle hdr_color;
    
    std::shared_ptr<Material> tonemap_material;
    std::shared_ptr<Material> shadow_debug_material;
    
    PipelineObject* shadow_debug_pipeline;
    PipelineObject* tonemap_pipeline;
    Engine* engine;
    
    bool capture_ibl;
    
};
REFLECT_OBJECT(GameRenderGraph, RenderGraph);