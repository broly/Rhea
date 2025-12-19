#pragma once
#include "framework/actor_component.h"
#include "math/transform.h"

class RhComp_Transform : public RhComponent
{
public:
    
    Transform transform;
    
    virtual void set_transform(const Transform& in_transform);
    Transform get_transform();
};

REG_REFLECT_ARGS(RhComp_Transform, RhComponent, 
    transform)