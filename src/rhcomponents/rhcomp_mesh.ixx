export module rhcomponents:rhcomp_mesh;

import rhmath;
import assets;
import framework;

import render_scene;

#include "object/object_reflection_macro.h"


export struct SceneViewProxy_Mesh : public SceneViewProxy_Transform
{
    MeshHandle mesh;
    PBRMaterial material;
    std::string debug_name;
};


export class RhComp_StaticMesh : public RhComp_Renderable
{
public:    
    RhComp_StaticMesh();
    
    void start() override;
    void finish() override;
    
    void update_scene_proxy();
    
    SceneViewProxy_Mesh scene_proxy;
    
    MeshHandle mesh;
    PBRMaterial material;
};

REFLECT_OBJECT_FIELDS(RhComp_StaticMesh, RhComp_Renderable, 
    transform, mesh, material);


