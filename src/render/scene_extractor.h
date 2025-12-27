#pragma once
#include <vector>

#include "renderer.h"
#include "render_id.h"
#include "framework/actor.h"
#include "render_objects.h"

class SceneExtractor
{
public:
    
    SceneExtractor(std::shared_ptr<class World> in_world, std::shared_ptr<class Renderer> in_renderer);

    void perform_extraction();
    
    RenderId register_ro_mesh();
    void unregister_ro_mesh(RenderId render_id);
    
    std::vector<RenderObject_Mesh> meshes;
    
    std::vector<RenderId> vacated_mesh_ids; 
    
    std::shared_ptr<class World> world;
    
    RenderMaterial get_or_create_material(const MaterialKey& material_key);
    
    std::unordered_map<MaterialKey, RenderMaterial, MaterialKeyHash> material_cache;

    std::shared_ptr<Renderer> renderer;
};
