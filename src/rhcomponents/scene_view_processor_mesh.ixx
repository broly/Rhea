export module rhcomponents:scene_view_proxy.mesh;

import render_scene;
import <cassert>;
import glm;
import <string>;
import assets;
import :rhcomp_mesh;

import render;
import name;
import rhmath;
import render;
import <unordered_map>;

#include "object/object_reflection_macro.h"

export struct RenderObject_Mesh
{
    MeshHandle mesh;
    glm::mat4 world;
    AABB bounds;
    std::vector<MaterialKey> material_keys;
    std::vector<RenderResourceInstance*> material_instances;
    std::vector<std::shared_ptr<MaterialInstance>> mat_instances;
    Name debug_name;
};



export class SceneViewProcessor_Mesh : public SceneViewProcessor
{
public:
    SceneViewProcessor_Mesh()
    {
        scene_proxy_size = scene_view_proxy_size_v<SceneViewProxy_Mesh>;
    }
    
    RenderId register_proxy() override;
    void unregister_proxy(RenderId render_id) override;
    void process() override;

    std::vector<RenderObject_Mesh> meshes;
    std::vector<RenderId> vacated_mesh_ids; 
    
    
    RenderResourceInstance* get_or_create_material_resource(RenderResource* resource, const MaterialKey& material_key, RBFrameHandle frame_handle = 0);
    
    std::unordered_map<MaterialKey, RenderResourceInstance*, MaterialKeyHash> material_cache;
};
REFLECT_OBJECT(SceneViewProcessor_Mesh, SceneViewProcessor);