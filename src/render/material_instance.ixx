export module render:material_instance;

import std.compat;

import :render_resource_instance;
import :renderer;
import assets;

export class MaterialManager;

export class MaterialInstance
{
public:
    
    MaterialInstance(const std::shared_ptr<const Material>& in_material, std::shared_ptr<Renderer> renderer,
        Name pass_name);
    // gives the material table slot back once the frames in flight are done (Renderer::collect_unused_materials)
    ~MaterialInstance();
    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;

    std::shared_ptr<const Material> material;

    void apply_material_parameters();

    std::shared_ptr<MaterialModel> model;
    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<MaterialManager> material_manager;

    uint32_t material_id;
};
