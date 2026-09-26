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
    };

    // Point light at the entity's position, or directional light along its forward axis
    struct Light
    {
        [[=rh::edit, =rh::color]] vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        [[=rh::edit, =rh::speed<0.05f>]] float intensity = 1.0f;
        [[=rh::edit, =rh::speed<0.01f>]] float falloff = 1.0f;
        [[=rh::edit]] LightType type = LightType::point;
    };
}
