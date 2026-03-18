export module render:material_instance;

import <memory>;
import :render_resource_instance;
import :renderer;
import assets;

export class MaterialManager;

export class MaterialInstance
{
public:
    
    MaterialInstance(const std::shared_ptr<const Material>& in_material, std::shared_ptr<Renderer> renderer, 
        Name pass_name, std::shared_ptr<PipelineFamily> in_pipeline_family);
    
    std::shared_ptr<const Material> material;

    void bind(RBCommandList cmd, RBFrameHandle frame);
    void apply_material_parameters(
        std::shared_ptr<RenderResourceInstance> instance,
        RBFrameHandle frame);

    std::shared_ptr<RenderResourceInstance> get_or_create_resource_instance(
        RBFrameHandle frame
    );

    RenderResource* render_resource;
    std::shared_ptr<RenderResourceInstance> render_resource_instance;
    
    std::shared_ptr<MaterialModel> model;
    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<PipelineFamily> pipeline_family;
    std::shared_ptr<MaterialManager> material_manager;
    
    std::map<Name, std::vector<std::byte>> cached_ubos;
    
    uint32_t material_id;
};
