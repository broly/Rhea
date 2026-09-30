#ifndef SKY_CLOUDS
#define SKY_CLOUDS

// Volumetric cloud layer ("Real-Time Volumetric Cloudscapes", A. Schneider): a shell around the planet
// between two altitudes, its density modelled from tiling noise volumes, ray marched with a second march
// towards the light per sample. Needs resources/sky.glsl.
//
// The march takes long steps through the empty air with a cheap density (no detail noise) and short
// ones with the full density inside the clouds: their edges and the detail noise need the short steps,
// most of a ray does not.

// Statistics of the shape noise (printed by tools/sky/generate_cloud_noise.py): the range of the base
// shape values and of the slice used as the weather map. SkyRenderer's coverage -> threshold mapping is
// calibrated for them.
const float CLOUD_BASE_MIN = 0.66;
const float CLOUD_BASE_MAX = 0.95;
const float CLOUD_WEATHER_MIN = 0.60;
const float CLOUD_WEATHER_MAX = 0.88;
const float CLOUD_SHAPE_TEXELS = 128.0;
const float CLOUD_DETAIL_TEXELS = 32.0;

// steps inside a cloud, in steps through the empty air
const float CLOUD_FINE_STEP = 1.0 / 3.0;
// empty samples in a row after which the march leaves a cloud
const int CLOUD_EMPTY_SAMPLES = 6;

// multiple scattering octaves of the sun light (cloud_march)
const int CLOUD_SCATTER_OCTAVES = 4;
const float CLOUD_OCTAVE_EXTINCTION = 0.45;
const float CLOUD_OCTAVE_ENERGY = 0.65;
const float CLOUD_OCTAVE_ANISOTROPY = 0.5;
// Deep inside a thick cloud the light arrives by diffusion: it falls off with the optical depth instead of
// exponentially (~ 1 / (1 + 3/4 (1 - g) depth) for a slab), which keeps the base of an overcast grey, not black
const float CLOUD_DIFFUSION = 0.11;
const float CLOUD_DIFFUSE_ENERGY = 0.15 / 3.14159265359;

// Altitude above the sphere through world y = 0 (the planet's axis goes through axis_xz: the camera)
float cloud_altitude(vec3 p, vec2 axis_xz)
{
    float r0 = sky_ubo.planet.x + sky_ubo.planet.z;
    vec2 d = p.xz - axis_xz;
    float q = dot(d, d) + p.y * (p.y + 2.0 * r0);
    return q / (sqrt(r0 * r0 + q) + r0);
}

// Roots of t^2 + 2 b t + c = 0 without cancellation, false when there are none
bool cloud_sphere_roots(float b, float c, out float t_near, out float t_far)
{
    float disc = b * b - c;
    if (disc <= 0.0)
        return false;
    float s = sqrt(disc);
    if (b > 0.0)
    {
        t_near = -(b + s);
        t_far = c / t_near;
    }
    else
    {
        t_far = s - b;
        t_near = c / t_far;
    }
    return true;
}

// Part [t0, t1] of a ray from world height origin_y which lies inside the cloud layer and in front of
// the planet
bool cloud_ray_interval(float origin_y, vec3 dir, out float t0, out float t1)
{
    float r0 = sky_ubo.planet.x + sky_ubo.planet.z;
    float bottom = sky_ubo.cloud_layer.x;
    float top = bottom + sky_ubo.cloud_layer.y;
    float b = (r0 + origin_y) * dir.y;

    float top_near, top_far;
    if (!cloud_sphere_roots(b, (origin_y - top) * (2.0 * r0 + origin_y + top), top_near, top_far) || top_far <= 0.0)
        return false;
    t0 = max(top_near, 0.0);
    t1 = top_far;

    float bottom_near, bottom_far;
    if (cloud_sphere_roots(b, (origin_y - bottom) * (2.0 * r0 + origin_y + bottom), bottom_near, bottom_far))
    {
        if (bottom_near <= t0 && bottom_far > t0)
            t0 = bottom_far;                // below the layer: it starts where the ray leaves the inner sphere
        else if (bottom_near > t0)
            t1 = min(t1, bottom_near);      // above its base: it ends there
    }

    float ground = -sky_ubo.planet.z;
    float ground_near, ground_far;
    if (cloud_sphere_roots(b, (origin_y - ground) * (2.0 * r0 + origin_y + ground), ground_near, ground_far)
        && ground_near > 0.0)
    {
        if (ground_near <= t0)
            return false;
        t1 = min(t1, ground_near);
    }
    return t1 > t0;
}

// Vertical profile of a cumulus: a flat base, the top thins out (only the strongest shapes reach it)
float cloud_profile(float height)
{
    return clamp(height / 0.07, 0.0, 1.0) * clamp((1.0 - height) / 0.6, 0.0, 1.0);
}

// Density in [0, 1] at a world position; height: its place in the layer, 0 base .. 1 top.
// lod, detail_lod: mips of the shape and of the detail noise; detail_lod < 0: the shape alone (cheaper,
// and never less than the full density: what it calls empty is empty)
float cloud_density(vec3 p, float height, float lod, float detail_lod)
{
    if (height <= 0.0 || height >= 1.0)
        return 0.0;

    float profile = cloud_profile(height);
    vec3 wp = p + sky_ubo.cloud_offset.xyz;

    // weather cells: the coverage varies over kilometers
    float weather = textureLod(u_cloud_shape, vec3(wp.xz * sky_ubo.cloud_scale.z, 0.5), 0.0).r;
    weather = clamp((weather - CLOUD_WEATHER_MIN) / (CLOUD_WEATHER_MAX - CLOUD_WEATHER_MIN), 0.0, 1.0);
    float threshold = clamp(sky_ubo.cloud_layer.z - (weather - 0.5) * sky_ubo.cloud_offset.w, 0.0, 0.999);
    if (profile <= threshold)
        return 0.0;

    // Perlin-Worley dilated by the Worley fbm: billowy shapes
    vec4 shape = textureLod(u_cloud_shape, wp * sky_ubo.cloud_scale.x, lod);
    float fbm = dot(shape.gba, vec3(0.625, 0.25, 0.125));
    float base = (shape.r + 1.0 - fbm) / (2.0 - fbm);
    base = clamp((base - CLOUD_BASE_MIN) / (CLOUD_BASE_MAX - CLOUD_BASE_MIN), 0.0, 1.0);

    float density = (base * profile - threshold) / (1.0 - threshold);
    if (density <= 0.0)
        return 0.0;

    if (detail_lod >= 0.0)
    {
        vec3 noise = textureLod(u_cloud_detail, (p + sky_ubo.cloud_detail_offset.xyz) * sky_ubo.cloud_scale.y, detail_lod).rgb;
        float detail_fbm = dot(noise, vec3(0.625, 0.25, 0.125));
        // wisps at the base, billows higher up
        float erosion = mix(detail_fbm, 1.0 - detail_fbm, clamp(height * 4.0, 0.0, 1.0)) * sky_ubo.cloud_scale.w;
        density = (density - erosion) / (1.0 - erosion);
    }
    return clamp(density, 0.0, 1.0);
}

// Mip of a noise volume for samples `step` apart: long steps skip texels, read the mip they cover
float cloud_lod(float step_length, float noise_scale, float texels)
{
    return clamp(log2(step_length * noise_scale * texels), 0.0, 4.0);
}

// Optical depth of the clouds between p and the light
float cloud_light_depth(vec3 p, vec2 axis_xz, vec3 light_dir, int steps)
{
    float bottom = sky_ubo.cloud_layer.x;
    float thickness = sky_ubo.cloud_layer.y;
    float reach = 0.5 * thickness;

    float depth = 0.0;
    for (int i = 0; i < steps; ++i)
    {
        // short steps first: the nearest cloud matters most
        float a0 = float(i) / float(steps);
        float a1 = float(i + 1) / float(steps);
        float s0 = reach * a0 * a0;
        float s1 = reach * a1 * a1;
        float step_length = s1 - s0;
        vec3 q = p + light_dir * (0.5 * (s0 + s1));
        float height = (cloud_altitude(q, axis_xz) - bottom) / thickness;
        // the detail matters next to the sample only
        float detail_lod = i < 2 ? cloud_lod(step_length, sky_ubo.cloud_scale.y, CLOUD_DETAIL_TEXELS) : -1.0;
        depth += cloud_density(q, height, cloud_lod(step_length, sky_ubo.cloud_scale.x, CLOUD_SHAPE_TEXELS), detail_lod) * step_length;
    }
    return depth * sky_ubo.cloud_layer.w;
}

// a low light shines through the layer for tens of kilometers: the shadow takes the clouds of this much of it
const float CLOUD_SHADOW_MAX_PATH = 20000.0;

// Share of the light from light_dir (above the horizon) which the cloud layer lets through along the whole
// line through `origin`, any point of it: what reaches everything below the clouds on that line.
// steps: samples through the layer.
float cloud_shadow_transmittance(vec3 origin, vec3 light_dir, int steps)
{
    if (sky_ubo.cloud_detail_offset.w < 0.5 || light_dir.y <= 0.0)
        return 1.0;

    float r0 = sky_ubo.planet.x + sky_ubo.planet.z;
    float bottom = sky_ubo.cloud_layer.x;
    float thickness = sky_ubo.cloud_layer.y;
    float top = bottom + thickness;
    float b = (r0 + origin.y) * light_dir.y;

    // The line goes up: it enters the layer where it leaves the sphere of its base and leaves it through the
    // sphere of its top. Behind the origin (t < 0) when that is above them: still ahead of what is lit.
    float t0, t1;
    if (!cloud_sphere_roots(b, (origin.y - top) * (2.0 * r0 + origin.y + top), t0, t1))
        return 1.0;
    float base_near, base_far;
    if (cloud_sphere_roots(b, (origin.y - bottom) * (2.0 * r0 + origin.y + bottom), base_near, base_far))
        t0 = base_far;
    t1 = min(t1, t0 + CLOUD_SHADOW_MAX_PATH);

    float step_length = (t1 - t0) / float(steps);
    float lod = cloud_lod(step_length, sky_ubo.cloud_scale.x, CLOUD_SHAPE_TEXELS);
    float detail_lod = cloud_lod(step_length, sky_ubo.cloud_scale.y, CLOUD_DETAIL_TEXELS);
    vec2 axis_xz = origin.xz;

    float depth = 0.0;
    for (int i = 0; i < steps; ++i)
    {
        vec3 p = origin + light_dir * (t0 + (float(i) + 0.5) * step_length);
        float height = (cloud_altitude(p, axis_xz) - bottom) / thickness;
        depth += cloud_density(p, height, lod, detail_lod);
    }
    return exp(-depth * step_length * sky_ubo.cloud_layer.w);
}

float cloud_phase_hg(float cos_theta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265359 * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4), 1.5));
}

// Clouds along a ray. rgb: the light they scatter towards the eye, a: the fraction of what is behind them
// that gets through. steps: long steps the layer is crossed in (at most 4 * steps samples are taken).
// jitter in [0, 1) offsets the samples (dithered, accumulated over frames).
// cloud_distance: how far the clouds are along the ray (where they start, when the ray meets none).
vec4 cloud_march(vec3 origin, vec3 dir, float jitter, int steps, int light_steps, out float cloud_distance)
{
    float max_distance = sky_ubo.cloud_march.x;
    cloud_distance = max_distance;
    if (sky_ubo.cloud_detail_offset.w < 0.5)
        return vec4(0.0, 0.0, 0.0, 1.0);

    float t0, t1;
    if (!cloud_ray_interval(origin.y, dir, t0, t1) || t0 >= max_distance)
        return vec4(0.0, 0.0, 0.0, 1.0);
    t1 = min(t1, max_distance);
    cloud_distance = t0;

    float bottom = sky_ubo.cloud_layer.x;
    float thickness = sky_ubo.cloud_layer.y;
    float sigma = sky_ubo.cloud_layer.w;
    vec2 axis_xz = origin.xz;

    float coarse_step = (t1 - t0) / float(steps);
    float fine_step = coarse_step * CLOUD_FINE_STEP;
    float coarse_lod = cloud_lod(coarse_step, sky_ubo.cloud_scale.x, CLOUD_SHAPE_TEXELS);
    float fine_lod = cloud_lod(fine_step, sky_ubo.cloud_scale.x, CLOUD_SHAPE_TEXELS);
    float detail_lod = cloud_lod(fine_step, sky_ubo.cloud_scale.y, CLOUD_DETAIL_TEXELS);

    vec3 light_dir = sky_ubo.light_direction.xyz;
    float cos_theta = dot(dir, light_dir);

    // Light scattered many times inside the cloud is what makes it white: octaves of the single scattering,
    // each with less extinction, less energy and a rounder phase function (Wrenninge's approximation)
    float phase[CLOUD_SCATTER_OCTAVES];
    float anisotropy = 1.0;
    for (int o = 0; o < CLOUD_SCATTER_OCTAVES; ++o)
    {
        phase[o] = mix(cloud_phase_hg(cos_theta, sky_ubo.cloud_phase.x * anisotropy),
                       cloud_phase_hg(cos_theta, -sky_ubo.cloud_phase.y * anisotropy), sky_ubo.cloud_phase.z);
        anisotropy *= CLOUD_OCTAVE_ANISOTROPY;
    }

    vec3 scatter = vec3(0.0);
    float transmittance = 1.0;
    float distance_sum = 0.0;
    float weight_sum = 0.0;

    float t = t0 + coarse_step * jitter;
    float marched = t0;         // the fine march got this far
    bool inside = false;
    int empty = 0;
    for (int i = 0; i < 4 * steps && t < t1; ++i)
    {
        vec3 p = origin + dir * t;
        float height = (cloud_altitude(p, axis_xz) - bottom) / thickness;

        if (!inside)
        {
            if (cloud_density(p, height, coarse_lod, -1.0) <= 0.0)
            {
                t += coarse_step;
                continue;
            }
            // a cloud: its edge is somewhere in the last long step
            inside = true;
            empty = 0;
            t = max(t - coarse_step, marched) + fine_step * jitter;
            continue;
        }

        float dt = fine_step;
        t += dt;
        marched = t;

        float density = cloud_density(p, height, fine_lod, detail_lod);
        if (density <= 0.0)
        {
            if (++empty >= CLOUD_EMPTY_SAMPLES)
                inside = false;
            continue;
        }
        empty = 0;

        float light_depth = cloud_light_depth(p, axis_xz, light_dir, light_steps);
        float energy = 0.0;
        float attenuation = 1.0;
        float contribution = 1.0;
        for (int o = 0; o < CLOUD_SCATTER_OCTAVES; ++o)
        {
            energy += contribution * phase[o] * exp(-light_depth * attenuation);
            attenuation *= CLOUD_OCTAVE_EXTINCTION;
            contribution *= CLOUD_OCTAVE_ENERGY;
        }
        energy = max(energy, CLOUD_DIFFUSE_ENERGY / (1.0 + CLOUD_DIFFUSION * light_depth));
        // thin edges facing the light scatter less than the beer law alone gives ("powder")
        float powder = 1.0 - exp(-2.0 * (light_depth + density * sigma * 60.0));
        energy *= mix(1.0, powder, sky_ubo.cloud_phase.w);

        // half of what a sample sees is the sky (less of it under the cloud above), half the ground
        // (less of it higher up); the dense core sees less of both
        float h = clamp(height, 0.0, 1.0);
        vec3 ambient = 0.5 * (sky_ubo.ambient_top.rgb * mix(0.35, 1.0, h) + sky_ubo.ambient_bottom.rgb * (1.0 - h));
        ambient *= sky_ubo.cloud_albedo.w * (1.0 - 0.5 * density);

        float extinction = max(density * sigma, 1e-6);
        vec3 source = (sky_ubo.light_color.rgb * energy + ambient) * sky_ubo.cloud_albedo.rgb * extinction;
        float step_transmittance = exp(-extinction * dt);
        // exact over the step for a constant source
        scatter += transmittance * (source - source * step_transmittance) / extinction;

        float weight = transmittance * (1.0 - step_transmittance);
        distance_sum += (t - 0.5 * dt) * weight;
        weight_sum += weight;

        transmittance *= step_transmittance;
        if (transmittance < 0.005)
        {
            transmittance = 0.0;
            break;
        }
    }

    if (weight_sum > 0.0)
        cloud_distance = distance_sum / weight_sum;

    // the air in front of far clouds: they take the color of the sky and dissolve before max_distance
    vec3 haze = 1.0 - exp(-(sky_ubo.rayleigh_scattering.rgb + sky_ubo.mie_extinction.rgb) * (0.5 * cloud_distance));
    scatter = mix(scatter, sky_ubo.ambient_top.rgb * (1.0 - transmittance), haze);
    float fade = smoothstep(0.6, 1.0, cloud_distance / max_distance);
    scatter *= 1.0 - fade;
    transmittance = mix(transmittance, 1.0, fade);

    return vec4(scatter, transmittance);
}

#endif // SKY_CLOUDS
