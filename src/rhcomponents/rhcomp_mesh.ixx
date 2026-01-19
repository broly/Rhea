export module rhcomponents:rhcomp_mesh;

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
    std::vector<PBRMaterial> materials;
    std::vector<std::shared_ptr<Material>> mats;
    // PBRMaterial material;
    Name debug_name;
};


export class RhComp_StaticMesh : public RhComp_Renderable
{
public:    
    
    void on_init() override;
    void start() override;
    void finish() override;
    void on_serialize(DependencyCollector* dc) override;
    
    void update_scene_proxy();
    
    SceneViewProxy_Mesh scene_proxy;
    
    std::vector<PBRMaterial> materials;
    
    MeshHandle mesh;

    std::vector<std::shared_ptr<Material>> mats;
};

REFLECT_OBJECT_FIELDS(RhComp_StaticMesh, RhComp_Renderable, 
    transform, materials, mesh, mats);


