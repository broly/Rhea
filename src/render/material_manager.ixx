export module render:material_manager;

import name;
import rhobject;
import :renderer;
import :render_resource;
import glm;
#include "object/object_reflection_macro.h"

export class GPUMaterial
{
    glm::vec4 base_color_factor;
    
    glm::vec4 params0;
    
    glm::ivec4 textures0;
};

export class MaterialManager : public RhObject
{
public:
    void ctor(const std::shared_ptr<Renderer>& in_renderer);
    
    
    uint32_t allocate_material(const GPUMaterial& material);
    
    void upload();
    
    std::vector<GPUMaterial> materials_cpu;
    
    bool dirty;

    Name material_resource_name;
    
    std::shared_ptr<Renderer> renderer;
    
    RenderResource* material_resource;
};
REFLECT_OBJECT_FIELDS(MaterialManager, RhObject,
    material_resource_name);