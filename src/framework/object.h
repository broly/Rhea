#pragma once
#include "object_reflection.h"
#include "math/transform.h"

class RhObject
{
public:
    virtual bool is_actor() const { return false; }
};

REG_REFLECT(RhObject);