export module render:renderer;

import <memory>;
import <map>;

import :handle_types;
import :render_objects;
import :render_backend;
import :material_schema;

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
    
    void load_schemas();

    virtual void execute() {}
    
    virtual void update_material_resource(RenderResourceInstance* material_resource_instance, MaterialKey material_key, RBFrameHandle frame);

    RBImageHandle create_texture_from_asset(TextureHandle handle);
    RBImageHandle get_texture(TextureHandle handle);
    
    virtual RenderResource* get_material_resource();
    
    std::shared_ptr<RenderBackend> render_backend;
    std::vector<std::shared_ptr<MaterialSchema>> schemas;
    
    std::map<TextureHandle, RBImageHandle> texture_cache;
    
    RenderResource* create_material_resource(const RenderResourceDesc& desc);
    
};
