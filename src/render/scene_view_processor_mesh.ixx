export module render:scene_view_proxy.mesh;

import render_scene;
import <cassert>;
import glm;
import <string>;
import assets;
import :render_objects;
import :render_resource;
import rhcomponents;
#include "scene_proxy_boilerplate.h"
import <unordered_map>;

export struct RenderObject_Mesh
{
    MeshHandle mesh;
    glm::mat4 world;
    AABB bounds;
    std::vector<MaterialKey> material_keys;
    std::vector<RenderResourceInstance*> material_instances;
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
    
    
    RenderResourceInstance* get_or_create_material_resource(RenderResource* resource, const MaterialKey& material_key, RBFrameHandle frame_handle = 0);
    
    std::unordered_map<MaterialKey, RenderResourceInstance*, MaterialKeyHash> material_cache;
};
REGISTER_SCENE_PROXY_PROCESSOR(SceneViewProcessor_Mesh)
