#ifndef SKY_ATMOSPHERE
#define SKY_ATMOSPHERE

// Physically based sky: single scattering of the sun light in a Rayleigh (air) + Mie (haze) atmosphere with
// an ozone layer around a spherical planet, integrated along the view ray, plus an approximation of the
// light scattered more than once (it fills the shadow of the planet: the twilight), the lit ground below the
// horizon, the sun and moon discs and stars.
// Needs resources/sky.glsl. The CPU estimates use the same model: AtmosphereModel (rhcomponents/sky.cpp).

#ifndef SKY_VIEW_STEPS
    #define SKY_VIEW_STEPS 16
#endif
#ifndef SKY_LIGHT_STEPS
    #define SKY_LIGHT_STEPS 6
#endif

const float SKY_PI = 3.14159265359;
const float SKY_OZONE_CENTER = 25000.0;
const float SKY_OZONE_HALF_WIDTH = 15000.0;

// Altitude above the ground of a point with |p|^2 - ground_radius^2 = q (no cancellation of the radius)
float sky_altitude(float q)
{
    float rg = sky_ubo.planet.x;
    return q / (sqrt(rg * rg + q) + rg);
}

// x: air, y: haze, z: ozone, relative to their reference densities
vec3 sky_density(float altitude)
{
    float h = max(altitude, 0.0);
    return vec3(
        exp(-h / sky_ubo.rayleigh_scattering.w),
        exp(-h / sky_ubo.mie_scattering.w),
        max(0.0, 1.0 - abs(h - SKY_OZONE_CENTER) / SKY_OZONE_HALF_WIDTH));
}

vec3 sky_extinction(vec3 density)
{
    return sky_ubo.rayleigh_scattering.rgb * density.x
        + sky_ubo.mie_extinction.rgb * density.y
        + sky_ubo.ozone_absorption.rgb * density.z;
}

// Distance from a point at radius r (ground radius + altitude) to the top of the atmosphere along a
// direction with cosine mu to the zenith
float sky_distance_to_top(float altitude, float mu)
{
    float r = sky_ubo.planet.x + altitude;
    float b = r * mu;
    float k = max((sky_ubo.planet.y - r) * (sky_ubo.planet.y + r), 0.0);
    float s = sqrt(b * b + k);
    return b > 0.0 ? k / (b + s) : s - b;
}

// Fraction of the light coming from outside the atmosphere along a direction (cosine mu to the zenith)
// that reaches a point at `altitude`. Zero when the planet is in the way.
vec3 sky_transmittance(float altitude, float mu)
{
    float rg = sky_ubo.planet.x;
    float r = rg + altitude;
    float b = r * mu;
    float c_ground = altitude * (2.0 * rg + altitude);
    if (mu < 0.0 && b * b - c_ground > 0.0)
        return vec3(0.0);

    float t_top = sky_distance_to_top(altitude, mu);
    vec3 tau = vec3(0.0);
    for (int i = 0; i < SKY_LIGHT_STEPS; ++i)
    {
        // denser near the point: the air is thickest there for rays going up
        float a0 = float(i) / float(SKY_LIGHT_STEPS);
        float a1 = float(i + 1) / float(SKY_LIGHT_STEPS);
        float u0 = t_top * a0 * a0;
        float u1 = t_top * a1 * a1;
        float u = 0.5 * (u0 + u1);
        float q = c_ground + u * (2.0 * b + u);
        tau += sky_extinction(sky_density(sky_altitude(q))) * (u1 - u0);
    }
    return exp(-tau);
}

// The same straight up, in closed form
vec3 sky_transmittance_zenith(float altitude)
{
    float h = max(altitude, 0.0);
    float rayleigh_height = sky_ubo.rayleigh_scattering.w;
    float mie_height = sky_ubo.mie_scattering.w;

    // ozone above h: the rest of a triangular profile
    float x = clamp(h, SKY_OZONE_CENTER - SKY_OZONE_HALF_WIDTH, SKY_OZONE_CENTER + SKY_OZONE_HALF_WIDTH);
    float below = max(SKY_OZONE_CENTER - x, 0.0);
    float above = SKY_OZONE_HALF_WIDTH - max(x - SKY_OZONE_CENTER, 0.0);
    float ozone_column = below - below * below / (2.0 * SKY_OZONE_HALF_WIDTH)
        + above * above / (2.0 * SKY_OZONE_HALF_WIDTH);

    vec3 tau = sky_ubo.rayleigh_scattering.rgb * (rayleigh_height * exp(-h / rayleigh_height))
        + sky_ubo.mie_extinction.rgb * (mie_height * exp(-h / mie_height))
        + sky_ubo.ozone_absorption.rgb * ozone_column;
    return exp(-tau);
}

// How much light scattered more than once there is at a point which sees the sun at cosine mu_sun to its
// zenith: it fades through the twilight, long after the direct light is gone
float sky_multiple_scattering(float mu_sun)
{
    float twilight = smoothstep(-0.28, 0.05, mu_sun);
    return twilight * twilight;
}

float sky_phase_rayleigh(float cos_theta)
{
    return 3.0 / (16.0 * SKY_PI) * (1.0 + cos_theta * cos_theta);
}

float sky_phase_hg(float cos_theta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * SKY_PI * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4), 1.5));
}

// Sun light scattered towards the eye along `dir` from world height world_y, and the ground when the ray
// ends on it. transmittance: of the whole path to space (zero when the ray hits the ground).
vec3 sky_atmosphere(float world_y, vec3 dir, out vec3 transmittance)
{
    float rg = sky_ubo.planet.x;
    float h0 = max(world_y + sky_ubo.planet.z, 1.0);
    float r0 = rg + h0;
    float b = r0 * dir.y;
    float c_ground = h0 * (2.0 * rg + h0);

    float t_max = sky_distance_to_top(h0, dir.y);
    bool hits_ground = false;
    float disc = b * b - c_ground;
    if (dir.y < 0.0 && disc > 0.0)
    {
        t_max = c_ground / (sqrt(disc) - b);
        hits_ground = true;
    }

    vec3 sun = sky_ubo.sun_direction.xyz;
    float cos_theta = dot(dir, sun);
    float phase_r = sky_phase_rayleigh(cos_theta);
    float phase_m = sky_phase_hg(cos_theta, sky_ubo.mie_extinction.w);
    // light scattered more than once comes from everywhere (isotropic) and mostly from above
    float multiple = sky_ubo.ozone_absorption.w / (4.0 * SKY_PI);

    vec3 tau = vec3(0.0);
    vec3 radiance = vec3(0.0);
    for (int i = 0; i < SKY_VIEW_STEPS; ++i)
    {
        float a0 = float(i) / float(SKY_VIEW_STEPS);
        float a1 = float(i + 1) / float(SKY_VIEW_STEPS);
        float t0 = t_max * a0 * a0;
        float t1 = t_max * a1 * a1;
        float t = 0.5 * (t0 + t1);
        float dt = t1 - t0;

        float altitude = sky_altitude(c_ground + t * (2.0 * b + t));
        vec3 density = sky_density(altitude);
        vec3 extinction = sky_extinction(density);
        vec3 view_transmittance = exp(-(tau + extinction * (0.5 * dt)));
        tau += extinction * dt;

        // the sun seen from the sample
        float mu_sun = clamp((r0 * sun.y + t * cos_theta) / (rg + altitude), -1.0, 1.0);
        vec3 sun_transmittance = sky_transmittance(altitude, mu_sun);

        vec3 rayleigh = sky_ubo.rayleigh_scattering.rgb * density.x;
        vec3 mie = sky_ubo.mie_scattering.rgb * density.y;
        radiance += view_transmittance * dt * (
            sun_transmittance * (rayleigh * phase_r + mie * phase_m)
            + sky_transmittance_zenith(altitude) * (sky_multiple_scattering(mu_sun) * multiple) * (rayleigh + mie));
    }
    radiance *= sky_ubo.sun_illuminance.rgb;

    vec3 path_transmittance = exp(-tau);
    transmittance = hits_ground ? vec3(0.0) : path_transmittance;

    if (hits_ground)
    {
        vec3 normal = normalize(vec3(0.0, r0, 0.0) + dir * t_max);
        float mu_sun = dot(normal, sun);
        vec3 irradiance = sky_ubo.sun_illuminance.rgb * sky_transmittance(0.0, mu_sun) * max(mu_sun, 0.0)
            + sky_ubo.ambient_top.rgb * SKY_PI;
        radiance += path_transmittance * sky_ubo.ground_albedo.rgb / SKY_PI * irradiance;
    }

    return radiance * sky_ubo.planet.w;
}

float sky_hash(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 sky_rotate(vec3 v, vec3 axis, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

// Star field: one star in some of the cells of a grid on the celestial sphere (which turns around the pole)
vec3 sky_stars(vec3 dir)
{
    const float cells = 170.0;
    vec3 p = sky_rotate(dir, sky_ubo.stars_rotation.xyz, sky_ubo.stars_rotation.w) * cells;
    vec3 cell = floor(p);
    float pick = sky_hash(cell);
    if (pick > 0.035)
        return vec3(0.0);

    // the whole star stays inside its cell
    vec3 star = vec3(sky_hash(cell + 17.0), sky_hash(cell + 31.0), sky_hash(cell + 59.0)) * 0.4 + 0.3;
    float d = length(fract(p) - star);
    float magnitude = sky_hash(cell + 101.0);
    float brightness = (0.15 + 0.85 * magnitude * magnitude * magnitude) * smoothstep(0.3, 0.0, d);
    // blue-white to warm
    vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.85, 0.7), sky_hash(cell + 7.0));
    return tint * brightness;
}

// 0 by day, 1 at night: stars fade in through the dusk
float sky_night_factor()
{
    return smoothstep(-0.06, -0.24, sky_ubo.sun_direction.y);
}

float sky_luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// What the atmosphere lets through along a view ray from world height world_y (zero when it ends on the ground)
vec3 sky_view_transmittance(float world_y, vec3 dir)
{
    return sky_transmittance(max(world_y + sky_ubo.planet.z, 1.0), dir.y);
}

// What shines through the atmosphere along `dir`: the night sky and the stars, dimmed by `visibility` (the
// transmittance of the path as one number), and with `discs` the sun and the moon
vec3 sky_space(float world_y, vec3 dir, float visibility, bool discs)
{
    vec3 space = sky_ubo.night_radiance.rgb;
    float night = sky_night_factor();
    if (night > 0.0 && sky_ubo.night_radiance.w > 0.0)
        space += sky_stars(dir) * (sky_ubo.night_radiance.w * night);
    space *= visibility;

    if (discs)
    {
        // the discs take the color the air leaves of them: the red sun at the horizon
        float sun_cos = dot(dir, sky_ubo.sun_direction.xyz);
        float sun_edge = sky_ubo.sun_direction.w;
        if (sun_cos > sun_edge)
        {
            // soft rim, darker towards the limb
            float x = (sun_cos - sun_edge) / max(1.0 - sun_edge, 1e-7);
            float disc = smoothstep(0.0, 0.12, x) * (0.55 + 0.45 * sqrt(clamp(x, 0.0, 1.0)));
            space += sky_ubo.sun_illuminance.rgb * (sky_ubo.sun_illuminance.w * disc) * sky_view_transmittance(world_y, dir);
        }

        vec3 moon = sky_ubo.moon_direction.xyz;
        float moon_cos = dot(dir, moon);
        float moon_edge = sky_ubo.moon_direction.w;
        if (dot(moon, moon) > 0.5 && moon_cos > moon_edge)
        {
            float x = (moon_cos - moon_edge) / max(1.0 - moon_edge, 1e-7);
            vec3 disc = sky_ubo.moon_radiance.rgb * (0.75 + 0.25 * sqrt(clamp(x, 0.0, 1.0))) * sky_view_transmittance(world_y, dir);
            // the moon hides the stars behind it
            space = mix(space, sky_ubo.night_radiance.rgb * visibility + disc, smoothstep(0.0, 0.1, x));
        }
    }

    return space * sky_ubo.planet.w;
}

// The sky behind the clouds along `dir`: atmosphere, night sky and, with `discs`, the sun and the moon
vec3 sky_radiance(float world_y, vec3 dir, bool discs)
{
    vec3 transmittance;
    vec3 radiance = sky_atmosphere(world_y, dir, transmittance);
    return radiance + sky_space(world_y, dir, sky_luminance(transmittance), discs);
}

#endif // SKY_ATMOSPHERE
