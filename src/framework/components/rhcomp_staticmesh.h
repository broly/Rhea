#pragma once
#include "rhcomp_renderable.h"
#include "rhcomp_transform.h"
#include "framework/object_reflection.h"

class RhComp_StaticMesh : public RhComp_Renderable
{
public:
    void start() override;
    
    std::string mesh_path;
    
    MeshHandle mesh;
};

REG_REFLECT_ARGS(RhComp_StaticMesh, RhComp_Renderable, 
    transform, mesh_path);
