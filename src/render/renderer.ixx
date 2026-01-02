export module render:renderer;

import <memory>;
import <map>;

import :handle_types;
import :render_objects;
import :render_backend;

export struct MaterialUBO
{
    glm::vec4 base_color;

    float metallic;
    float roughness;

    int has_normal;
    int has_occlusion;
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
    void update_material_descriptor(const RenderMaterial& rm, const MaterialKey& key);
    
    RBDescriptorSet allocate_material_descriptor();
    RBBufferHandle create_material_ubo();

    void bind_material_ubo(const RenderMaterial& rm);

    virtual void execute() {}

    RBImageHandle create_texture_from_asset(TextureHandle handle);
    RBImageHandle get_texture(TextureHandle handle);
    
    std::shared_ptr<RenderBackend> render_backend;
    
    std::map<TextureHandle, RBImageHandle> texture_cache;
    RBDescriptorSetLayout material_layout;
    RBDescriptorSetLayout light_layout;
    
};
