export module framework:rhcomp_light;

import :rhcomponent;

import rhmath;
import assets;
import :rhcomp_transform;
import :rhcomp_renderable;

#include "object/object_reflection_macro.h"



export class RhComp_Light : public RhComp_Renderable
{
public:    
    void start() override;
    void finish() override;

    vec4 color;
    float intensity;
    float falloff;
};

REFLECT_OBJECT_FIELDS(RhComp_Light, RhComp_Renderable, 
    transform, color, intensity, falloff);
