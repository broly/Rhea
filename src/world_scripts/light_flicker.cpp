module;

#include <json/value.h>

module light_flicker;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import rhcomponents;
import sky_controller;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    // [0, 1]
    float lattice(int x)
    {
        uint32_t h = uint32_t(x) * 0x9e3779b9u;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        return float(h & 0xffffffu) * (1.0f / 16777215.0f);
    }

    // smooth value noise, [0, 1]
    float noise(float x)
    {
        const float cell = std::floor(x);
        float f = x - cell;
        f = f * f * (3.0f - 2.0f * f);
        return glm::mix(lattice(int(cell)), lattice(int(cell) + 1), f);
    }

    // After the sky controller: it drives the directional light, this one the point lights
    [[=ecs::system<ecs::Phase::Update>, =ecs::after<SkyControl>]]
    void flicker_lights(ecs::Query<Light, LightFlicker> lights, ecs::Res<ecs::FrameTime> time)
    {
        lights.each([&] (ecs::Entity e, Light& light, LightFlicker& flicker) {
            if (flicker.phase < 0.0f)
                flicker.phase = lattice(int(e.bits())) * 1000.0f;

            // a slow swell with quick flutters on it
            const float t = float(time->time) * flicker.speed + flicker.phase;
            const float dip = 0.6f * noise(t) + 0.3f * noise(t * 2.7f + 17.0f) + 0.1f * noise(t * 6.1f + 41.0f);
            const float dim = std::clamp(flicker.amount, 0.0f, 1.0f) * dip;
            const float brightness = 1.0f - dim;
            // a lower flame is a cooler one: green and blue drop faster than red
            const float warmth = std::clamp(flicker.warmth, 0.0f, 1.0f) * dim;
            const glm::vec3 tint(1.0f, 1.0f - 0.6f * warmth, 1.0f - warmth);

            const glm::vec4 color = flicker.color.glm();
            light.color = glm::vec4(glm::vec3(color) * tint * brightness, color.w);
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(LightFlicker)
}
