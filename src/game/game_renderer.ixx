export module game:renderer;
import <memory>;
import render;
import engine;
import <map>;
import <vector>;
import name;
import assets;

export class GameRenderer : public Renderer
{
public:    
    void set_engine(const std::shared_ptr<Engine>& in_engine);

    void init(RBWindowHandle in_window) override;
    void execute() override;

    std::shared_ptr<Engine> engine;
    
    std::shared_ptr<RenderGraph> render_graph;
    
    void draw_scene(RenderGraphContext& ctx);
    void draw_scene_shadow(RenderGraphContext& ctx);
    
    std::shared_ptr<MaterialInstance> get_or_create_material_instance(std::shared_ptr<Material> material, Name pass_name);

    PipelineLayoutDesc geom_pipeline_layout;
    PipelineLayoutDesc shadow_pipeline_layout;

    RenderResource* camera_resource;
    RenderResource* light_resource;
    RenderResource* light_resource_shadow;
    RenderResource* shadow_resource;
    
    RGTextureHandle shadow_map;
    
    PipelineObject* geom_pipeline;
    
    std::map<std::pair<std::shared_ptr<Material>, Name>, std::shared_ptr<MaterialInstance>> material_instances;
};
