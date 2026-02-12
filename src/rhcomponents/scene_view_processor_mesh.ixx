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


export using RenderPrimitiveId = uint32_t;

export struct RenderObject_Mesh
{
    MeshHandle mesh;
    glm::mat4 world;
    AABB bounds;

    std::vector<RenderPrimitiveId> primitives;
    bool dirty;
};

struct RenderPrimitive
{
    RenderPrimitiveId id;

    MeshPrimHandle mesh;
    const glm::mat4* world;
    AABB bounds;

    std::map<Name, std::shared_ptr<MaterialInstance>> material_instance_by_pass;
    
    std::map<Name, PipelineObject*> pipeline_by_pass;

    uint8_t dirty;
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
    
    std::vector<RenderPrimitive> primitives;
    
    std::vector<RenderPrimitiveId> free_primitive_ids;
};
REFLECT_OBJECT(SceneViewProcessor_Mesh, SceneViewProcessor);