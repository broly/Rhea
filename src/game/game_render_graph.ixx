export module game:render_graph;

import render;
import glm;
import name;
import <map>;
import <memory>;
import engine;

class GameRenderGraph : public RenderGraph
{
public:
    GameRenderGraph(
        const std::shared_ptr<RenderBackend>& in_backend,
        const std::shared_ptr<Renderer>& in_renderer);
    
    
    bool is_debugging() const;
    
    glm::mat4 build_dir_light_vp() const;
    
    void draw_scene(RenderGraphContext& ctx);
    void draw_scene_shadow(RenderGraphContext& ctx);
    void draw_clouds(RenderGraphContext& ctx, RGTextureHandle depth_texture, RGTextureHandle noise_texture);
    
    CameraUBO make_camera_ubo(RenderGraphContext& ctx, bool zero_pos = false) const;
    
    RGTextureHandle shadow_map;
    RBSwapchainExtent shadowmap_extent;
    
    RenderResource* camera_resource;
    RenderResource* light_resource;
    RenderResource* light_resource_shadow;
    RenderResource* shadow_resource;
    
    Engine* engine;
    
};