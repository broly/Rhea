#pragma once
#include "rhcomp_renderable.h"
#include "rhcomp_transform.h"
#include "framework/material.h"
#include "framework/object_reflection.h"
#include "framework/assets/texture.h"
#include "framework/assets/mesh.h"

class RhComp_StaticMesh : public RhComp_Renderable
{
public:
    void start() override;
    
    MeshHandle mesh;
    TextureHandle texture;
    PBRMaterial material;
};

REFLECT_OBJECT_FIELDS(RhComp_StaticMesh, RhComp_Renderable, 
    transform, mesh, texture, material);
