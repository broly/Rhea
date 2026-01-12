export module framework:rhcomponent;

import rhobject;
import :core;
import <memory>;

#include "object/object_reflection_macro.h"


export class RhComponent : public RhObject
{
public:
    void on_add(std::shared_ptr<RhActor> actor);
    
    virtual void start();
    virtual void finish();
    virtual void tick(double dt);
    
    virtual void on_serialize(DependencyCollector* dc);
    
    std::shared_ptr<RhActor> owner;
    
    void set_owner(std::shared_ptr<RhActor> actor);
};

REFLECT_OBJECT(RhComponent, RhObject)
