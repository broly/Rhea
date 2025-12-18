#pragma once
#include "rhcomp_transform.h"
#include "framework/object_reflection.h"

class RhComp_StaticMesh : public RhComp_Transform
{
public:
    std::string mesh_path;
};

REG_REFLECT_ARGS(RhComp_StaticMesh, RhComp_Transform, 
    transform, mesh_path);
