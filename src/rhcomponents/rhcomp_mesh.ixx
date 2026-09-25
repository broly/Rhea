module;

#include <json/value.h>

export module rhcomponents:rhcomp_mesh;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import rhmath;
import assets;
import framework;
import name;
import render_scene;
import render;

#include "object/object_reflection_macro.h"


export struct SceneViewProxy_Mesh : public SceneViewProxy_Transform
{
    MeshHandle mesh;
    AABB bounds;
    std::vector<std::shared_ptr<Material>> materials;
    Name debug_name;
    
    // set for skinned meshes (RhComp_SkeletalMesh)
    std::shared_ptr<SkinningPose> skinning;
};


export class RhComp_StaticMesh : public RhComp_Renderable
{
public:    
    
    void on_init() override;
    void start() override;
    void finish() override;
    void on_serialize(const SerializationContext& context) override;
    
    AABB get_aabb() const override;
    
    virtual void update_scene_proxy();
    
    SceneViewProxy_Mesh scene_proxy;
    
    MeshHandle mesh;

    std::vector<std::shared_ptr<Material>> mats;
};

REFLECT_OBJECT_FIELDS(RhComp_StaticMesh, RhComp_Renderable, 
    transform, mesh, mats);


