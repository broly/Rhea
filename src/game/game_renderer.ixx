export module game:renderer;
import <memory>;
import render;
import engine;
import <vector>;

export class GameRenderer : public Renderer
{
public:
    GameRenderer()
    {
    }
    
    void set_engine(const std::shared_ptr<Engine>& in_engine);

    void init(RBWindowHandle in_window) override;
    void execute() override;
    
    RenderResource* get_material_resource() override;
    
    void update_material_resource(RenderResourceInstance* material_resource_inst, MaterialKey material_key, RBFrameHandle frame) override;
    
    std::shared_ptr<Engine> engine;
    
    std::shared_ptr<RenderGraph> render_graph;
    
    RenderResource* _mat_res;
    PipelineObject* geom_pipeline;
};
