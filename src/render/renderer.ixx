export module render:renderer;

import <memory>;
import <map>;

import :handle_types;
import :render_objects;
import :render_backend;
import :material_model;
import :pipeline_family;

#include "common/reflect_macros.h"

export struct MaterialUBO
{
    float base_color_factor;
    float emissive_factor;
    float occlusion_factor;
    float roughness_factor;
    float metallic_factor;
};
REFLECT_STRUCT_RUNTIME(MaterialUBO,
    base_color_factor, emissive_factor, occlusion_factor, roughness_factor, metallic_factor);

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


export class Renderer : public std::enable_shared_from_this<Renderer>
{
public:
    virtual ~Renderer() = default;
    virtual void init(RBWindowHandle in_window);
    
    void load_schemas();

    virtual void execute() {}

    RBImageHandle create_texture_from_asset(TextureHandle handle);
    RBImageHandle get_texture(TextureHandle handle);
    
    std::shared_ptr<PipelineFamily> get_or_create_material_pipeline_family(Name pass_name, const std::shared_ptr<MaterialModel>& model_name);
    
    RenderResource* get_or_create_resource_from_model(std::shared_ptr<MaterialModel> model, Name pass_name);
    
    mutable std::map<std::pair<Name, std::shared_ptr<MaterialModel>>, std::shared_ptr<PipelineFamily>> material_pipeline_families;

    std::shared_ptr<RenderBackend> render_backend;
    std::map<Name, std::shared_ptr<MaterialModel>> models;
    
    std::map<TextureHandle, RBImageHandle> texture_cache;
    
    RenderResource* create_material_resource(const RenderResourceDesc& desc);
    
    std::map<std::pair<Name, std::shared_ptr<MaterialModel>>, RenderResource*> material_resources;
    
    std::map<Name, RBSampler> samplers;
    
};
