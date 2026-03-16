module render:material_manager;
#include "common/assertion_macros.h"

void MaterialManager::ctor(const std::shared_ptr<Renderer>& in_renderer)
{
    renderer = in_renderer;
    
    material_resource = renderer->find_resource(material_resource_name);
    checkf(material_resource, "MaterialSSBO not found");
    
    
}

uint32_t MaterialManager::allocate_material(const GPUMaterial& material)
{
    uint32_t index = (uint32_t)materials_cpu.size();

    materials_cpu.push_back(material);

    dirty = true;

    return index;
}

void MaterialManager::upload()
{
    if (!dirty)
        return;

    size_t size = materials_cpu.size() * sizeof(GPUMaterial);
    
    material_resource->update_ssbo("material_ssbo", size, materials_cpu.data());

    dirty = false;
}
