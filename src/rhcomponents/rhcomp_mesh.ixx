export module rhcomponents:rhcomp_mesh;

import rhmath;
import assets;
import framework;

import render_scene;

#include "object/object_reflection_macro.h"


export struct SceneViewProxy_Mesh : public SceneViewProxy_Transform
{
    MeshHandle mesh;
    std::vector<PBRMaterial> materials;
    // PBRMaterial material;
    std::string debug_name;
    bool auto_retrieve_materials;
};


export class RhComp_StaticMesh : public RhComp_Renderable
{
public:    
    RhComp_StaticMesh();
    
    void start() override;
    void finish() override;
    
    void update_scene_proxy();
    
    SceneViewProxy_Mesh scene_proxy;
    
    std::map<std::string, PBRMaterial> materials;
    
    MeshHandle mesh;
    
    bool auto_retrieve_materials = false;
};

REFLECT_OBJECT_FIELDS(RhComp_StaticMesh, RhComp_Renderable, 
    transform, materials, mesh, auto_retrieve_materials);


