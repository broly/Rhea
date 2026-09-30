export module rhcomponents:sky;

import std.compat;
import reflect;

import glm;
import rhmath;
import render_scene;

export
{
    // Sky of the level: a physically based atmosphere (Rayleigh / Mie scattering of the sun light, ozone), the
    // sun and moon discs, stars. Drawn behind the scene, captured by the reflection probes and the source of
    // their fallback ambient (game: SkyRenderer, shaders/sky/atmosphere.glsl).
    //
    // The sun is the level's directional Light: its disc is drawn where that light comes from and the
    // atmosphere is lit from there. sun_color * sun_intensity is the sun above the atmosphere (what the sky
    // scatters), the Light's color is the same sun as it reaches the ground. A SkyController keeps the two
    // consistent through the day and takes over the celestial directions (night: the Light is the moon).
    //
    //     "SkyAtmosphere": { "sun_intensity": 10, "mie": 1.5 }
    struct SkyAtmosphere
    {
        [[=rh::edit, =rh::color]] vec3 sun_color{ 1.0f, 0.98f, 0.95f };
        [[=rh::edit, =rh::speed<0.05f>]] float sun_intensity = 10.0f;
        // angular diameter, degrees (the real sun: 0.53)
        [[=rh::edit, =rh::range<0.1f, 10.f>]] float sun_disc_size = 1.4f;
        // radiance of the disc per unit of sun light (a physical disc would be ~1 / its solid angle: far
        // above what a half float target and the reflections take)
        [[=rh::edit, =rh::speed<0.5f>]] float sun_disc_intensity = 60.0f;

        // strength of the air scattering (1: Earth) and its color balance: the blue of the sky, red sunsets
        [[=rh::edit, =rh::range<0.f, 4.f>]] float rayleigh = 1.0f;
        [[=rh::edit, =rh::color]] vec3 rayleigh_color{ 0.175f, 0.409f, 1.0f };
        // haze: aerosols near the ground, the white glow around the sun and at the horizon
        [[=rh::edit, =rh::range<0.f, 10.f>]] float mie = 1.0f;
        [[=rh::edit, =rh::range<0.f, 0.99f>]] float mie_anisotropy = 0.8f;
        // ozone absorption: keeps the zenith blue at sunset
        [[=rh::edit, =rh::range<0.f, 4.f>]] float ozone = 1.0f;
        // light scattered more than once, as a fraction of the single scattering
        [[=rh::edit, =rh::range<0.f, 2.f>]] float multiple_scattering = 0.6f;
        // overall scale of the sky radiance
        [[=rh::edit, =rh::range<0.f, 4.f>]] float intensity = 1.0f;

        // what is seen below the horizon
        [[=rh::edit, =rh::color]] vec3 ground_color{ 0.18f, 0.17f, 0.15f };
        [[=rh::edit, =rh::speed<10.f>]] float planet_radius = 6360.0f;     // km
        // altitude of world y = 0 above the planet surface, m
        [[=rh::edit, =rh::speed<1.f>]] float ground_altitude = 200.0f;

        // night sky without the moon (airglow), radiance
        [[=rh::edit, =rh::color]] vec3 night_color{ 0.0006f, 0.0009f, 0.0018f };
        [[=rh::edit, =rh::range<0.f, 8.f>]] float stars_intensity = 1.0f;
        [[=rh::edit, =rh::range<0.1f, 10.f>]] float moon_disc_size = 1.8f;  // degrees
        [[=rh::edit, =rh::color]] vec3 moon_color{ 0.75f, 0.82f, 1.0f };
        [[=rh::edit, =rh::speed<0.01f>]] float moon_disc_intensity = 0.6f;  // radiance of the disc

        // Set by a SkyController (driven): where the sun and the moon are, and the one of them which lights
        // the clouds with its color above them - independent of the directional light, which is what reaches
        // the ground (dimmed by the weather, tinted by gameplay).
        // Not driven: the sun is where the directional light comes from and lights the clouds, no moon.
        [[=rh::transient]] bool driven = false;
        [[=rh::transient]] vec3 sun_direction{ 0.0f, 1.0f, 0.0f };   // towards the sun
        [[=rh::transient]] vec3 moon_direction{ 0.0f, 0.0f, 0.0f };  // towards the moon, zero: none
        [[=rh::transient]] vec3 cloud_light_direction{ 0.0f, 1.0f, 0.0f };
        [[=rh::transient]] vec3 cloud_light_color{ 0.0f, 0.0f, 0.0f };
        [[=rh::transient]] float stars_rotation = 0.0f;              // rad, around the celestial pole
        [[=rh::transient]] vec3 stars_axis{ 0.0f, 0.7071f, -0.7071f };
    };

    // Layer of volumetric clouds over the level, on the entity of its SkyAtmosphere: ray marched through
    // tiling noise volumes (shaders/sky/clouds.glsl), lit by the directional light and the sky.
    // The clouds drift with the wind (advance_clouds, render_sync.cpp).
    //
    //     "VolumetricClouds": { "coverage": 0.6, "wind_speed": 15, "wind_direction": 40 }
    struct VolumetricClouds
    {
        [[=rh::edit]] bool enabled = true;
        // fraction of the sky the clouds take: 0 clear, 0.5 scattered, 0.75 overcast with gaps, 1 solid
        [[=rh::edit, =rh::range<0.f, 1.f>]] float coverage = 0.6f;
        // how much the coverage varies over the weather cells (0: even everywhere)
        [[=rh::edit, =rh::range<0.f, 1.f>]] float coverage_variation = 0.85f;
        // optical thickness: 1 white cumulus, more for heavy rain clouds
        [[=rh::edit, =rh::range<0.f, 4.f>]] float density = 1.0f;

        [[=rh::edit, =rh::speed<10.f>]] float bottom = 1500.0f;        // m above world y = 0
        [[=rh::edit, =rh::speed<10.f>]] float thickness = 2200.0f;     // m

        // sizes of the noise tiles, m: cloud shapes, their edge detail, the weather (coverage) cells
        [[=rh::edit, =rh::speed<10.f>]] float shape_size = 8000.0f;
        [[=rh::edit, =rh::speed<10.f>]] float detail_size = 900.0f;
        [[=rh::edit, =rh::speed<100.f>]] float weather_size = 42000.0f;
        // how deep the detail noise cuts into the shapes
        [[=rh::edit, =rh::range<0.f, 1.f>]] float detail = 0.5f;

        // the clouds move towards wind_direction (degrees from -Z, clockwise seen from above) at wind_speed m/s
        [[=rh::edit, =rh::range<0.f, 360.f>]] float wind_direction = 60.0f;
        [[=rh::edit, =rh::speed<0.1f>]] float wind_speed = 12.0f;
        // the edge detail drifts through the shapes this much faster: the clouds keep changing
        [[=rh::edit, =rh::range<0.f, 4.f>]] float turbulence = 0.6f;

        [[=rh::edit, =rh::color]] vec3 albedo{ 1.0f, 1.0f, 1.0f };
        // forward scattering of the sun light: the bright (silver) lining around the sun
        [[=rh::edit, =rh::range<0.f, 0.95f>]] float anisotropy = 0.65f;
        // sky light on the clouds
        [[=rh::edit, =rh::range<0.f, 4.f>]] float ambient = 1.0f;
        // darkens thin edges facing the sun (the "powdered sugar" look of cumulus)
        [[=rh::edit, =rh::range<0.f, 1.f>]] float powder = 0.5f;
        // clouds further than this dissolve into the haze, m
        [[=rh::edit, =rh::speed<100.f>]] float max_distance = 60000.0f;
        // how much of the directional light the clouds take from what is below them: 0 no cloud shadows,
        // 1 nothing but ambient light under a thick cloud (the rest stands for the light it scatters down)
        [[=rh::edit, =rh::range<0.f, 1.f>]] float shadow = 0.8f;

        // accumulated wind, m (runtime)
        [[=rh::transient]] vec3 offset{ 0.0f, 0.0f, 0.0f };
        [[=rh::transient]] vec3 detail_offset{ 0.0f, 0.0f, 0.0f };
    };

    struct SceneViewProxy_Sky : public SceneViewProxy_Transform
    {
        SkyAtmosphere atmosphere;
        VolumetricClouds clouds;    // enabled = false when the entity has none
    };

    // The atmosphere as the renderer and the CPU estimates use it (SI units: meters, 1 / m).
    // Must match shaders/sky/atmosphere.glsl.
    struct AtmosphereModel
    {
        glm::vec3 rayleigh_scattering{ 0.0f };      // at the ground
        float rayleigh_height = 8000.0f;            // scale height
        glm::vec3 mie_scattering{ 0.0f };
        float mie_height = 1200.0f;
        glm::vec3 mie_extinction{ 0.0f };
        float mie_anisotropy = 0.8f;
        glm::vec3 ozone_absorption{ 0.0f };         // at the center of the ozone layer
        float multiple_scattering = 0.0f;
        float planet_radius = 6360000.0f;
        float atmosphere_radius = 6420000.0f;
        float ground_altitude = 0.0f;               // of world y = 0
        float intensity = 1.0f;
        glm::vec3 ground_albedo{ 0.0f };

        static AtmosphereModel from(const SkyAtmosphere& sky);

        // Fraction of the light arriving from outside the atmosphere along `direction` (towards the source)
        // that reaches a point at world height y (zero when the planet is in the way)
        glm::vec3 transmittance(float world_y, const glm::vec3& direction) const;

        // Radiance of the sky seen from world height y along `direction`: the sun light (illuminance above the
        // atmosphere) scattered by the air, and the lit ground below the horizon. No discs, stars or clouds.
        glm::vec3 radiance(float world_y, const glm::vec3& direction, const glm::vec3& sun_direction,
            const glm::vec3& sun_illuminance) const;
    };
}
