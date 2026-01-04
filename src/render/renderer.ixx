export module render:renderer;

import <memory>;
import <map>;

import :handle_types;
import :render_objects;
import :render_backend;

export struct MaterialUBO
{
    float base_color_mult;
    float emissive_mult;
    float occlusion_mult;
    float roughness_mult;
    float metallic_mult;
};

export struct Light
{
    glm::vec4 position;
    glm::vec4 color;
};

export struct LightUBO
{
    Light lights[8];
    int light_count;
};


export class Renderer
{
public:
    virtual ~Renderer() = default;
    virtual void init(RBWindowHandle in_window);
    virtual void update_material_descriptor(const RenderMaterial& rm, const MaterialKey& key);
    
    virtual RBDescriptorSet allocate_material_descriptor();
    virtual RBBufferHandle create_material_ubo();
    
    virtual RBDescriptorSetLayout get_material_layout() const;;

    virtual void bind_material_ubo(const RenderMaterial& rm);

    virtual void execute() {}

    RBImageHandle create_texture_from_asset(TextureHandle handle);
    RBImageHandle get_texture(TextureHandle handle);
    
    std::shared_ptr<RenderBackend> render_backend;
    
    std::map<TextureHandle, RBImageHandle> texture_cache;
    
};
