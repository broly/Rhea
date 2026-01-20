module render:material_instance;
import :material;
#include "common/assertion_macros.h"

MaterialInstance::MaterialInstance(const std::shared_ptr<const Material>& in_material, Renderer* renderer)
    : material(in_material)
{
    Name model_name = material->model;
    auto model_it = renderer->models.find(model_name);
    checkf(model_it != renderer->models.end(), "Could not find specified model");
    
    // pipeline_family = renderer->get_or_create_material_pipeline_family(model_name);
    
    std::shared_ptr<MaterialModel> model = model_it->second;
    
    
}
