export module rhcomponents:light;

import std.compat;
import reflect;

import rhmath;
import render_scene;

export
{
    enum class LightType
    {
        point,
        directional
    };

    struct SceneViewProxy_Light : public SceneViewProxy_Transform
    {
        LightType light_type;
        vec4 color;
        float intensity;
        float falloff;
        bool visible_in_reflection_probes = true;
    };

    // Point light at the entity's position, or directional light along its forward axis
    struct Light
    {
        [[=rh::edit, =rh::color]] vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        [[=rh::edit, =rh::speed<0.05f>]] float intensity = 1.0f;
        [[=rh::edit, =rh::speed<0.01f>]] float falloff = 1.0f;
        [[=rh::edit]] LightType type = LightType::point;
        // baked into the reflection probes (their captures, see ReflectionCapture). Off for dynamic lights
        // (character light rig, flashes): a probe keeps what it captured until it is rebaked
        [[=rh::edit]] bool visible_in_reflection_probes = true;
    };
}
