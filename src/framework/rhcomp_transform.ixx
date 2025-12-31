export module framework:rhcomp_transform;

import :rhcomponent;

import rhmath;
import assets;

#include "object/object_reflection_macro.h"

export class RhComp_Transform : public RhComponent
{
public:
    
    Transform transform;
    
    virtual void set_transform(const Transform& in_transform);
    Transform get_transform();
};

REFLECT_OBJECT_FIELDS(RhComp_Transform, RhComponent, 
    transform);