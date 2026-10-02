module;

#include <json/value.h>

export module gameplay:effects;

import std.compat;
import reflect;
import rhobject;

// One shot effects (muzzle flashes, tracers, impacts): prefabs with a ParticleSystem that remove themselves.
//
//     "Lifetime": { "seconds": 0.4 }
export struct Lifetime
{
    [[=rh::edit, =rh::speed<0.01f>]] float seconds = 1.0f;
    [[=rh::edit, =rh::read_only]] float age = 0.0f;
};
