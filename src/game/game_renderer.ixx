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
    
    
    virtual void update_material_descriptor(const RenderMaterial& rm, const MaterialKey& key);
    
    virtual RBDescriptorSet allocate_material_descriptor();
    virtual RBBufferHandle create_material_ubo();
    RBDescriptorSetLayout get_material_layout() const override;

    virtual void bind_material_ubo(const RenderMaterial& rm);
    
    RBBufferHandle camera_buffer;
    RBBufferHandle light_buffer;
    
    RBDescriptorSetLayout camera_layout;
    RBDescriptorSetLayout tonemap_layout;
    RBDescriptorSetLayout material_layout;
    RBDescriptorSetLayout light_layout;
    
    std::shared_ptr<Engine> engine;
    
    std::shared_ptr<RenderGraph> render_graph;
};
