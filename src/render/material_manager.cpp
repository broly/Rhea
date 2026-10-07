module render;

import :material_manager;

import std.compat;
import assertions;
import glm;
#include "common/assertion_macros.h"

void MaterialManager::ctor(const std::shared_ptr<Renderer>& in_renderer)
{
    renderer = in_renderer;
    
    material_resource = renderer->find_resource(material_resource_name);
    checkf(material_resource, "MaterialSSBO not found");
    
    const RenderResourceVariableDesc& materials_var = material_resource->find_var_checked("materials");
    capacity_bytes = materials_var.parameter.initial_buffer_size.value_or(0);
    
    
}

uint32_t MaterialManager::allocate_material()
{
    GPUMaterial material{
        glm::vec4{0.0f},
        glm::vec4{0.0f},
        glm::uvec4(0)
    };

    uint32_t index;
    if (!free_materials.empty())
    {
        index = free_materials.back();
        free_materials.pop_back();
        materials_cpu[index] = material;
    }
    else
    {
        index = (uint32_t)materials_cpu.size();
        materials_cpu.push_back(material);
    }

    dirty = true;

    return index;
}

void MaterialManager::free_material(uint32_t index)
{
    checkf(index < materials_cpu.size(), "Material %u was never allocated", index);
    free_materials.push_back(index);
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
    checkf(size <= capacity_bytes,
        "Material SSBO overflow: %zu materials need %zu bytes, capacity is %zu (increase initial_buffer_size of '%s')",
        materials_cpu.size(), size, capacity_bytes, material_resource_name.to_string().c_str());
    
    material_resource->update_ssbo("materials", size, materials_cpu.data());

    dirty = false;
}

const GPUMaterial& MaterialManager::get_gpu_material(uint32_t index)
{
    return materials_cpu[index];
}
