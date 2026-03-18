module render:material_manager;
import glm;
#include "common/assertion_macros.h"

void MaterialManager::ctor(const std::shared_ptr<Renderer>& in_renderer)
{
    renderer = in_renderer;
    
    material_resource = renderer->find_resource(material_resource_name);
    checkf(material_resource, "MaterialSSBO not found");
    
    
}

uint32_t MaterialManager::allocate_material()
{
    const uint32_t index = (uint32_t)materials_cpu.size();

    GPUMaterial material{
        glm::vec4{0.0f},
        glm::vec4{0.0f},
        glm::uvec4(0)
    };
    materials_cpu.push_back(material);

    dirty = true;

    return index;
}

void MaterialManager::update_material(uint32_t index, const GPUMaterial& material)
{
    materials_cpu[index] = material;
    upload();
}

void MaterialManager::upload()
{
    if (!dirty)
        return;

    const size_t size = materials_cpu.size() * sizeof(GPUMaterial);
    
    material_resource->update_ssbo("materials", size, materials_cpu.data());

    dirty = false;
}

const GPUMaterial& MaterialManager::get_gpu_material(uint32_t index)
{
    return materials_cpu[index];
}
