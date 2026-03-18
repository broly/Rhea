export module render:material_manager;

import name;
import rhobject;
import :renderer;
import :render_resource;
import glm;
#include "object/object_reflection_macro.h"

export struct GPUMaterial
{
    glm::vec4 params0 = glm::vec4{0.f};
    
    glm::vec4 params1 = glm::vec4{0.f};
    
    glm::uvec4 textures0 = glm::uvec4{0};
};
REFLECT_STRUCT_RUNTIME(GPUMaterial,
    params0, params1, textures0);

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
    
    std::shared_ptr<Renderer> renderer;
    RenderResource* material_resource;
    
    
public: // to serialize
    
    Name material_resource_name;
};
REFLECT_OBJECT_FIELDS(MaterialManager, RhObject,
    material_resource_name);