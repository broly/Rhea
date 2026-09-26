export module rhcomponents:camera;

import std.compat;
import reflect;

import rhmath;
import render_scene;

export
{
    struct SceneViewProxy_Camera : public SceneViewProxy_Transform
    {
        float fov = 0.f;
        float near_plane = 0.f;
        float far_plane = 0.f;
        bool active = false;
    };

    // Perspective camera at the entity's WorldTransform; the active one renders the view
    struct Camera
    {
        [[=rh::edit, =rh::degrees, =rh::range<10.f, 120.f>]] float fov = 1.0f;
        [[=rh::edit, =rh::speed<0.001f>]] float near_plane = 0.1f;
        [[=rh::edit, =rh::speed<1.f>]] float far_plane = 1000.0f;
        [[=rh::edit]] bool active = false;
    };
}
