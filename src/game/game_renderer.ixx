export module game:renderer;
import <memory>;
import render;
import engine;
import <map>;
import <vector>;
import name;
import glm;
import assets;

export class GameRenderer : public Renderer
{
public:    
    void set_engine(const std::shared_ptr<Engine>& in_engine);

    void init(RBWindowHandle in_window) override;

    std::shared_ptr<Engine> engine;
    
    bool is_debugging() const;
    
    glm::mat4 build_dir_light_vp() const;
    
    void draw_scene(RenderGraphContext& ctx);
    void draw_scene_shadow(RenderGraphContext& ctx);
    
    RenderResource* camera_resource;
    RenderResource* light_resource;
    RenderResource* light_resource_shadow;
    RenderResource* shadow_resource;
    
    RGTextureHandle shadow_map;
    RBSwapchainExtent shadowmap_extent;
    
};
