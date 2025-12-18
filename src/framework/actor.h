#pragma once
#include "actor_component.h"
#include "object.h"
#include "math/transform.h"

class RhActor : public RhObject
{
public:
    Transform transform;
    
    virtual ~RhActor() = default;
    
    bool is_actor() const override
    {
        return true;
    }
    
    virtual void initialize();
    
    virtual void start() {}
    virtual void tick(const double DeltaTime) {}
    virtual void finish() {}
    
    std::vector<std::shared_ptr<RhComponent>> ComponentInstances;
};

REG_REFLECT_ARGS(RhActor, transform);