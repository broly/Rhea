module;

#include <json/value.h>

export module light_flicker;

import std.compat;
import reflect;
import rhobject;
import rhmath;

// Flicker of the entity's point Light (fire, torches, failing lamps): drives Light::color around `color`.
// The light dims by up to `amount` of its brightness in an irregular rhythm, and turns redder as it dims,
// like a flame that burns lower.
//
//     "Light": { "type": "point", "visible_in_reflection_probes": false },
//     "LightFlicker": { "color": { "x": 14, "y": 6, "z": 1.6, "w": 0 }, "amount": 0.45, "speed": 9 }
export struct LightFlicker
{
    // the light at its brightest (Light::color is overwritten every frame)
    [[=rh::edit, =rh::color]] vec4 color{ 1.0f, 1.0f, 1.0f, 0.0f };
    // share of the brightness lost at the deepest dip
    [[=rh::edit, =rh::range<0.f, 1.f>]] float amount = 0.3f;
    // dips per second, roughly
    [[=rh::edit, =rh::speed<0.1f>]] float speed = 8.0f;
    // how much redder the dimmed light is: 0 keeps the hue
    [[=rh::edit, =rh::range<0.f, 1.f>]] float warmth = 0.5f;

    // where this light is in the rhythm: lights of one fire do not flicker in step
    [[=rh::transient]] float phase = -1.0f;
};
