export module rhcomponents:reflection_capture;

import std.compat;
import reflect;

import rhmath;
import assets;
import render_scene;

export
{
    struct SceneViewProxy_ReflectionCapture : public SceneViewProxy_Transform
    {
        bool active = false;
        CubemapHandle irradiance;
        CubemapHandle prefiltered_env;
    };

    // Image based lighting probe at the entity's position. Its cubemaps are captured with G and stored as
    // hdr/ibl_<entity name>_{irradiance,prefiltered_env}.exr, loaded when the probe is registered.
    struct ReflectionCapture
    {
        [[=rh::edit]] bool active = false;

        [[=rh::transient]] CubemapHandle irradiance;
        [[=rh::transient]] CubemapHandle prefiltered_env;
    };
}
