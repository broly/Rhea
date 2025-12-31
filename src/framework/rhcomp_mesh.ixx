export module framework:rhcomp_mesh;

import :rhcomponent;

import rhmath;
import assets;
import :rhcomp_transform;
import :rhcomp_renderable;

#include "object/object_reflection_macro.h"

export class RhComp_StaticMesh : public RhComp_Renderable
{
public:    
    void start() override;
    void finish() override;
    
    MeshHandle mesh;
    PBRMaterial material;
};

REFLECT_OBJECT_FIELDS(RhComp_StaticMesh, RhComp_Renderable, 
    transform, mesh, material);


