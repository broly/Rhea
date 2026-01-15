export module render:material_instance;

import <memory>;

export class Material;

export class MaterialInstance
{
public:
    
    MaterialInstance(const std::shared_ptr<const Material>& in_material);
    
    
    std::shared_ptr<const Material> material;
};
