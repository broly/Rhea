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


export class Renderer
{
public:
    virtual ~Renderer() = default;
    virtual void init(RBWindowHandle in_window);
    void update_material_descriptor(const RenderMaterial& rm, const MaterialKey& key);
    
    virtual void execute() {}

    RBImageHandle create_texture_from_asset(TextureHandle handle);
    RBImageHandle get_texture(TextureHandle handle);
    
    std::shared_ptr<RenderBackend> render_backend;
    
    std::map<TextureHandle, RBImageHandle> texture_cache;
    
};
