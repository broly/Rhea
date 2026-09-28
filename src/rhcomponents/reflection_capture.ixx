export module rhcomponents:reflection_capture;

import std.compat;
import reflect;

import rhmath;
import render_scene;

export
{
    struct SceneViewProxy_ReflectionCapture : public SceneViewProxy_Transform
    {
        bool active = false;
        vec3 box_extent;
        vec3 box_offset;
        float blend_distance = 1.0f;
        float intensity = 1.0f;
        float update_interval = 0.0f;
        bool rebake_on_enter = false;
    };

    // Reflection probe at the entity's position, baked at runtime by the renderer (ReflectionProbeSystem,
    // src/game/reflection_probes.cpp): the scene is rendered into a cube from the entity position a face per
    // frame, then filtered into the probe's specular / irradiance cubes. Rebaked when the probe moves, when
    // the sun changes, every update_interval seconds and on request (Render panel / G).
    //
    // The box (position + box_offset, +-box_extent, world axes) is both the influence volume and the parallax
    // proxy: fit it to the room / street around the probe. Surfaces inside the box (faces included) get this
    // probe's reflections with the box as the reflected geometry; the influence fades out over blend_distance
    // outside the box, overlapping influences are blended, and space no box reaches interpolates between
    // all probes.
    struct ReflectionCapture
    {
        [[=rh::edit]] bool active = true;
        [[=rh::edit, =rh::speed<0.05f>]] vec3 box_extent{5.0f, 4.0f, 5.0f};
        [[=rh::edit, =rh::speed<0.05f>]] vec3 box_offset{0.0f, 0.0f, 0.0f};
        [[=rh::edit, =rh::speed<0.01f>]] float blend_distance = 1.0f;
        [[=rh::edit, =rh::speed<0.01f>]] float intensity = 1.0f;
        // seconds between automatic rebakes, 0: only when something changes
        [[=rh::edit, =rh::speed<0.1f>]] float update_interval = 0.0f;
        // rebake whenever the camera enters the box (picks up moved objects around key points)
        [[=rh::edit]] bool rebake_on_enter = false;
    };
}
