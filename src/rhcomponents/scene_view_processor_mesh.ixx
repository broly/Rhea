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

    std::shared_ptr<MaterialInstance> material_instance;
    
    std::shared_ptr<PipelineFamily> pipeline_family;
    std::shared_ptr<PipelineFamily> shadow_pipeline_family;
    PipelineObject* pipeline;
    PipelineObject* shadow_pipeline;
    
    Name pass_name;

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