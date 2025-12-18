#pragma once
#include "framework/actor_component.h"
#include "math/transform.h"

class RhComp_Transform : public RhComponent
{
public:
    Transform transform;
};

REG_REFLECT_ARGS(RhComp_Transform, RhComponent, 
    transform)