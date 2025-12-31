export module game:renderer;
import <memory>;
import render;
import engine;


export class GameRenderer : public Renderer
{
public:
    GameRenderer()
        : camera_buffer(0)
    {
    }
    
    void set_engine(const std::shared_ptr<Engine>& in_engine);

    void init(RBWindowHandle in_window) override;
    void execute() override;
    
    RBBufferHandle camera_buffer;
    RBDescriptorSetLayout camera_layout;
    RBBufferHandle light_buffer;
    
    std::shared_ptr<Engine> engine;
    
    std::shared_ptr<RenderGraph> render_graph;
};
