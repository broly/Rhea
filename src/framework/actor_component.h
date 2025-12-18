#pragma once
#include "object.h"

class RhActor;

class RhComponent : public RhObject
{
public:
    void on_add(const std::shared_ptr<RhActor>& actor)
    {
        owner = actor;
    }
    
    virtual void start();
    virtual void finish();
    virtual void tick(double dt);
    
    std::shared_ptr<RhActor> owner;
};

REG_REFLECT(RhComponent, RhObject)
