export module engine:scene_extractor;
import <vector>;

import render;
import framework;
import <memory>;
import render;
import <unordered_map>;

export class SceneExtractor
{
public:
    
    SceneExtractor(std::shared_ptr<class World> in_world, std::shared_ptr<class Renderer> in_renderer);

    void perform_extraction();
    
    RenderId register_ro_mesh();
    void unregister_ro_mesh(RenderId render_id);
    
    void register_camera(std::shared_ptr<Camera> in_camera);
    
    std::vector<RenderObject_Mesh> meshes;
    
    std::vector<RenderId> vacated_mesh_ids; 
    
    std::shared_ptr<class World> world;
    
    RenderMaterial get_or_create_material(const MaterialKey& material_key);
    
    std::unordered_map<MaterialKey, RenderMaterial, MaterialKeyHash> material_cache;

    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<Camera> camera;
};
