module;

#include <json/value.h>

export module gameplay:effects;

import std.compat;
import glm;
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

// A JPEG spot for a moment where game rules say (a bite at a jackal's muzzle), besides the spots of the blows
// (DamageEvent::jpeg_preset). The game's damage feedback hands it to the JPEG hits (game:jpeg_hits).
// preset: assets/jpeg/presets.json, section "hits"; radius / lifetime <= 0: the preset's.
export struct JpegSpotEvent
{
    glm::vec3 position{ 0.0f };
    std::string preset = "default";
    float strength = 1.0f;
    float radius = 0.0f;
    float lifetime = 0.0f;
};
