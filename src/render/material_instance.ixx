export module render:material_instance;

import <memory>;
import :render_resource_instance;
import :renderer;
import assets;


export class MaterialInstance
{
public:
    
    MaterialInstance(const std::shared_ptr<const Material>& in_material, std::shared_ptr<Renderer> renderer, Name pass_name);
    
    std::shared_ptr<const Material> material;

    void bind(PipelineObject* pipeline, RBCommandList cmd, RBFrameHandle frame);
    void apply_material_parameters(
        RenderResourceInstance* instance,
        PipelineObject* pipeline,
        RBFrameHandle frame);
    
    std::map<Name, RenderResourceInstance*> render_resource_instances_per_pass; // TODO (wtf?) by pass or by pipeline?
    
    RenderResourceInstance* get_or_create_resource_instance(
        PipelineObject* pipeline,
        RBFrameHandle frame
    );

    RenderResource* render_resource;
    std::unordered_map<PipelineObject*, std::unique_ptr<RenderResourceInstance>> instances;
    
    std::shared_ptr<MaterialModel> model;
    std::shared_ptr<Renderer> renderer;
    
    std::map<Name, std::vector<std::byte>> cached_ubos;
};
