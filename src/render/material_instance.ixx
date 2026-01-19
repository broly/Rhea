export module render:material_instance;

import <memory>;
import :render_resource_instance;
import :renderer;

export class Material;

export class MaterialInstance
{
public:
    
    MaterialInstance(const std::shared_ptr<const Material>& in_material, Renderer* renderer);
    
    
    std::shared_ptr<const Material> material;
    RenderResourceInstance* render_resource_instance = nullptr;
};
