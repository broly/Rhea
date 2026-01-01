export module render:scene_view_proxy.mesh;

import render_scene;
import <cassert>;
import glm;
import <string>;
import assets;
import :render_objects;
import rhcomponents;
#include "scene_proxy_boilerplate.h"
import <unordered_map>;

export struct RenderObject_Mesh
{
    MeshHandle mesh;
    glm::mat4 world;
    AABB bounds;
    RenderMaterial material;
    std::string debug_name;
};



export class SceneViewProcessor_Mesh : public SceneViewProcessor
{
public:
    SCENE_PROXY_BOILERPLATE(SceneViewProcessor_Mesh, SceneViewProcessor, SceneViewProxy_Mesh, 0);
    
    RenderId register_proxy() override;
    void unregister_proxy(RenderId render_id) override;
    void process() override;

    std::vector<RenderObject_Mesh> meshes;
    std::vector<RenderId> vacated_mesh_ids; 
    
    
    RenderMaterial get_or_create_material(const MaterialKey& material_key);
    
    std::unordered_map<MaterialKey, RenderMaterial, MaterialKeyHash> material_cache;
};
REGISTER_SCENE_PROXY_PROCESSOR(SceneViewProcessor_Mesh)
