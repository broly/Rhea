export module framework:rhcomp_renderable;

import :rhcomp_transform;
import rhmath;
import assets;

import render_scene;

#include "object/object_reflection_macro.h"

export class RhComp_Renderable : public RhComp_Transform
{
public:
    RhComp_Renderable();
    
    void start() override;
    void finish() override;
    
    
};

REFLECT_OBJECT(RhComp_Renderable, RhComp_Transform);

