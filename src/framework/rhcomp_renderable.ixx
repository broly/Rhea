module;

#include <json/value.h>

export module framework:rhcomp_renderable;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import :rhcomp_transform;
import rhmath;
import assets;


#include "object/object_reflection_macro.h"

export class RhComp_Renderable : public RhComp_Transform
{
public:
    RhComp_Renderable();
    
    void start() override;
    void finish() override;
    
    virtual AABB get_aabb() const;
    
    
};

RH_OBJECT(RhComp_Renderable)

