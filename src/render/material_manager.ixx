module;

#include <json/value.h>

export module render:material_manager;

import std.compat;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import name;
import rhobject;
import :renderer;
import :render_resource;
import glm;
#include "object/object_reflection_macro.h"

// Shared by every material model (layout must match resources/pbr_material_table.glsl).
// The first 48 bytes keep the legacy PBR layout; material models map their named
// parameters onto members via `material_info` in the schema.
export struct GPUMaterial
{
    glm::vec4 params0 = glm::vec4{0.f};
    
    glm::vec4 params1 = glm::vec4{0.f};
    
    glm::uvec4 textures0 = glm::uvec4{0};
    
    glm::uvec4 textures1 = glm::uvec4{0};
    
    glm::vec4 params2 = glm::vec4{0.f};
    glm::vec4 params3 = glm::vec4{0.f};
    glm::vec4 params4 = glm::vec4{0.f};
    glm::vec4 params5 = glm::vec4{0.f};
    glm::vec4 params6 = glm::vec4{0.f};
    glm::vec4 params7 = glm::vec4{0.f};
    glm::vec4 params8 = glm::vec4{0.f};
    glm::vec4 params9 = glm::vec4{0.f};
    glm::vec4 params10 = glm::vec4{0.f};
    glm::vec4 params11 = glm::vec4{0.f};
    glm::vec4 params12 = glm::vec4{0.f};
    glm::vec4 params13 = glm::vec4{0.f};
    glm::vec4 params14 = glm::vec4{0.f};
    glm::vec4 params15 = glm::vec4{0.f};
};
static_assert(sizeof(GPUMaterial) == 288);
REFLECT_STRUCT_RUNTIME(GPUMaterial,
    params0, params1, textures0, textures1,
    params2, params3, params4, params5, params6, params7, params8, params9,
    params10, params11, params12, params13, params14, params15);

export class MaterialManager : public RhObject
{
public:
    void ctor(const std::shared_ptr<Renderer>& in_renderer);
    
    
    uint32_t allocate_material();
    
    void update_material(uint32_t index, const GPUMaterial& material);
    
    void upload();
    
    const GPUMaterial& get_gpu_material(uint32_t index);
    
    std::vector<GPUMaterial> materials_cpu;
    
    bool dirty;
    
    // capacity of the material SSBO (from the resource description)
    size_t capacity_bytes = 0;
    
    std::shared_ptr<Renderer> renderer;
    RenderResource* material_resource;
    
    
public: // to serialize
    
    Name material_resource_name;
};
REFLECT_OBJECT_FIELDS(MaterialManager, RhObject,
    material_resource_name);