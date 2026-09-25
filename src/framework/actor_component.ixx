module;

#include <json/value.h>

export module framework:rhcomponent;

import std.compat;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import rhobject;
import :core;

#include "object/object_reflection_macro.h"


export class RhComponent : public RhObject
{
public:
    void on_add(std::shared_ptr<RhActor> actor);
    
    virtual void start();
    virtual void finish();
    virtual void tick(double dt);
    
    std::shared_ptr<RhActor> owner;
    
    void set_owner(std::shared_ptr<RhActor> actor);
};

RH_OBJECT(RhComponent)
