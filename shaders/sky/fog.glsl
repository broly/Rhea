#ifndef SKY_FOG
#define SKY_FOG

// Volumetric fog (VolumetricFog, SkyRenderer): two media marched along the view ray from the eye
//   ground fog  full density up to a base height, thinning out exponentially above it, broken up into
//               banks and drifting wisps (the shape noise of the clouds): it lies in the low ground
//   haze        thin, everywhere, thinning out with the altitude
// lit by the directional light through the shadow cascades (the scene) and the shadow of the clouds - the lit
// and the shadowed air along the ray are the light shafts - and by what the reflection probes hold of the
// light around (the sky in the open, the dim light indoors).
// Needs resources/sky.glsl, resources/light.glsl, resources/shadow.glsl and resources/reflection.glsl.

const float FOG_PI = 3.14159265359;

// range of the shape noise's Perlin-Worley channel (5th .. 95th percentile) and the size of its volume
const float FOG_NOISE_MIN = 0.60;
const float FOG_NOISE_MAX = 0.86;
const float FOG_NOISE_TEXELS = 128.0;
// the layer is thin: its wisps are flatter than the noise
const float FOG_NOISE_FLATTEN = 3.0;
// the banks: shapes this many wisps across which thicken, thin out and lift the layer
const float FOG_BANK_SIZE = 4.0;
const float FOG_BANK_STRENGTH = 0.9;
const float FOG_WISP_STRENGTH = 0.5;

// share of the forward lobe in the phase function, the rest scatters evenly: the shafts show from the side too
const float FOG_FORWARD_WEIGHT = 0.7;
// The light leaves the thick ground fog after many scatterings: its glow towards the sun is wider than the
// haze's (anisotropy of the ground fog, in anisotropies of the haze)
const float FOG_GROUND_ANISOTROPY = 0.45;
// the light from around (probes) is looked up once in this many samples of a ray
const int FOG_AMBIENT_INTERVAL = 8;

// Ground fog at height h above its base, relative to the density at the base
float fog_profile(float h, float falloff)
{
    return exp(-max(h, 0.0) / falloff);
}

// Integral of fog_profile from the base up to h (m): negative below the base
float fog_profile_integral(float h, float falloff)
{
    return h < 0.0 ? h : falloff * (1.0 - exp(-h / falloff));
}

// Mean of fog_profile over a segment between the heights ha and hb: exact, however thin the layer is
// against the segment
float fog_profile_mean(float ha, float hb, float falloff)
{
    float dh = hb - ha;
    if (abs(dh) < 1e-2)
        return fog_profile(0.5 * (ha + hb), falloff);
    return (fog_profile_integral(hb, falloff) - fog_profile_integral(ha, falloff)) / dh;
}

// Integral of fog_profile from h up (m): the fog above a point
float fog_profile_above(float h, float falloff)
{
    return falloff - fog_profile_integral(h, falloff);
}

float fog_noise_value(vec3 q, float lod)
{
    float value = textureLod(u_cloud_shape, q, lod).r;
    return clamp((value - FOG_NOISE_MIN) / (FOG_NOISE_MAX - FOG_NOISE_MIN), 0.0, 1.0);
}

// Banks and wisps of the ground fog at a point. x: factor of the density (about 1 on average), y: how far the
// layer is lifted here (m). step_length: distance between the samples of the march (picks the mip of the noise)
vec2 fog_noise(vec3 p, float step_length)
{
    float strength = sky_ubo.fog_noise.y;
    if (strength <= 0.0)
        return vec2(1.0, 0.0);

    vec3 q = (p + sky_ubo.fog_offset.xyz) * sky_ubo.fog_noise.x;
    q.y *= FOG_NOISE_FLATTEN;
    float lod = clamp(log2(max(step_length * sky_ubo.fog_noise.x * FOG_NOISE_TEXELS, 1.0)), 0.0, 5.0);

    float wisps = fog_noise_value(q, lod);
    float bank = fog_noise_value(q / FOG_BANK_SIZE + 0.37, max(lod - log2(FOG_BANK_SIZE), 0.0));

    float density = 1.0 + strength * (FOG_BANK_STRENGTH * (2.0 * bank - 1.0) + FOG_WISP_STRENGTH * (2.0 * wisps - 1.0));
    return vec2(max(density, 0.0), (bank - 0.5) * sky_ubo.fog_noise.z);
}

// Share of the directional light the clouds let through to a point. Like cloud_shadow (resources/shadow.glsl),
// but beyond the map it is what the clouds let through on average (a ray of the march leaves the map, and the
// haze there must not light up), and at full strength: the light a cloud scatters down, which softens its
// shadow on the ground, comes from all over the sky - it is not what shows as a shaft.
float fog_cloud_shadow(vec3 p)
{
    if (light_ubo.dir_light.cloud_shadow_params.x <= 0.0)
        return 1.0;

    vec2 uv = vec2(dot(p, light_ubo.dir_light.cloud_shadow_u.xyz) + light_ubo.dir_light.cloud_shadow_u.w,
                   dot(p, light_ubo.dir_light.cloud_shadow_v.xyz) + light_ubo.dir_light.cloud_shadow_v.w);
    float edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    float average = sky_ubo.fog_haze.w;
    float transmittance = average;
    if (edge > 0.0)
        transmittance = mix(average, textureLod(u_cloud_shadow, uv, 0.0).r, clamp(edge * 16.0, 0.0, 1.0));
    return transmittance;
}

// Light which arrives at a point from the directions above it and from those below, each as the radiance of
// its half: what the reflection probes hold around the point (the sky probe in the open)
void fog_ambient(vec3 p, out vec3 top, out vec3 bottom)
{
    float weights[MAX_REFLECTION_PROBES];
    float sky_weight;
    if (!reflection_probe_weights(p, weights, sky_weight))
    {
        top = reflection_probes_ubo.sky_ambient.rgb;
        bottom = sky_ubo.ambient_bottom.rgb;
        return;
    }

    const uint count = min(reflection_probes_ubo.info.x, uint(MAX_REFLECTION_PROBES));
    top = vec3(0.0);
    bottom = vec3(0.0);
    for (uint i = 0u; i < count; ++i)
    {
        if (weights[i] < 1e-3)
            continue;
        float weight = weights[i] * reflection_probes_ubo.probes[i].box_max.w;
        top += weight * textureLod(u_probe_irradiance[i], vec3(0.0, 1.0, 0.0), 0.0).rgb;
        bottom += weight * textureLod(u_probe_irradiance[i], vec3(0.0, -1.0, 0.0), 0.0).rgb;
    }
    if (sky_weight >= 1e-3)
    {
        top += sky_weight * textureLod(u_probe_irradiance[reflection_probes_ubo.info.y - 1u], vec3(0.0, 1.0, 0.0), 0.0).rgb;
        // the sky probe holds no ground
        bottom += sky_weight * sky_ubo.ambient_bottom.rgb;
    }
}

// What is left of the light after an optical depth of fog. Fog scatters nearly all of the light it takes:
// much of it arrives anyway, by a longer way (the second term)
float fog_light_transmittance(float depth)
{
    return 0.6 * exp(-depth) + 0.4 * exp(-0.2 * depth);
}

float fog_phase_hg(float cos_theta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * FOG_PI * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4), 1.5));
}

float fog_phase(float cos_theta, float g)
{
    return mix(1.0 / (4.0 * FOG_PI), fog_phase_hg(cos_theta, g), FOG_FORWARD_WEIGHT);
}

// How far the fog in front of the sky is marched along a ray: to its max distance, and no further than the
// base of the clouds (the cloud shadow map holds what reaches the air below them)
float fog_sky_distance(vec3 origin, vec3 dir)
{
    float t = sky_ubo.fog_haze.y;
    float bottom = sky_ubo.cloud_layer.x;
    if (sky_ubo.cloud_detail_offset.w > 0.5 && dir.y > 1e-4 && origin.y < bottom)
        t = min(t, (bottom - origin.y) / dir.y);
    return t;
}

// Fog along a ray up to t_end. rgb: the light it scatters towards the eye, a: the fraction of what is behind
// it that gets through. The samples are closer together near the eye; jitter in [0, 1) places them inside
// their steps (dithered, accumulated over frames).
// fog_distance: how far the fog is along the ray, by what it adds to the pixel (t_end when it adds nothing).
vec4 fog_march(vec3 origin, vec3 dir, float t_end, float jitter, int steps, out float fog_distance)
{
    fog_distance = t_end;
    float sigma_ground = sky_ubo.fog_layer.x;
    float sigma_haze = sky_ubo.fog_layer.w;
    if (sky_ubo.fog_offset.w < 0.5 || t_end <= 0.0)
        return vec4(0.0, 0.0, 0.0, 1.0);

    float base = sky_ubo.fog_layer.y;
    float falloff = 1.0 / sky_ubo.fog_layer.z;
    float inv_haze_height = sky_ubo.fog_haze.x;

    bool has_light = light_ubo.has_dir_light == 1;
    vec3 light_dir = -normalize(light_ubo.dir_light.direction.xyz);
    vec3 light = light_ubo.dir_light.color.rgb;
    float cos_theta = dot(dir, light_dir);
    float anisotropy = sky_ubo.fog_albedo.w;
    // (with the scale of the light shafts: the directional light in the haze)
    float phase_haze = fog_phase(cos_theta, anisotropy) * sky_ubo.fog_noise.w;
    float phase_ground = fog_phase(cos_theta, anisotropy * FOG_GROUND_ANISOTROPY);
    // the light crosses the layer above a sample at a slant
    float slant = 1.0 / max(light_dir.y, 0.08);

    // the light of each half of the directions, scattered evenly: half of its mean radiance
    float ambient_scale = 0.5 * sky_ubo.fog_haze.z;
    vec3 ambient_top = vec3(0.0);
    vec3 ambient_bottom = vec3(0.0);

    vec3 scatter = vec3(0.0);
    float transmittance = 1.0;
    float distance_sum = 0.0;
    float weight_sum = 0.0;

    float t0 = 0.0;
    for (int i = 0; i < steps; ++i)
    {
        float a = float(i + 1) / float(steps);
        float t1 = t_end * a * a;
        float dt = t1 - t0;
        float t = t0 + dt * jitter;
        vec3 p = origin + dir * t;

        if (i % FOG_AMBIENT_INTERVAL == 0)
            fog_ambient(p, ambient_top, ambient_bottom);

        vec2 noise = fog_noise(p, dt);
        float lift = base + noise.y;
        float ground = sigma_ground * noise.x
            * fog_profile_mean(origin.y + dir.y * t0 - lift, origin.y + dir.y * t1 - lift, falloff);
        float haze = sigma_haze * exp(-max(p.y, 0.0) * inv_haze_height);
        float extinction = ground + haze;
        t0 = t1;
        if (extinction * dt < 1e-5)
            continue;

        // the fog above the sample takes from what lights it from above
        float above = sigma_ground * noise.x * fog_profile_above(p.y - lift, falloff);
        vec3 source = (ambient_top * fog_light_transmittance(above) + ambient_bottom) * ambient_scale;
        if (has_light)
        {
            float visibility = fog_cloud_shadow(p);
            if (visibility > 0.0)
                visibility *= cascade_shadow_volume(p);
            float phase = (phase_ground * ground + phase_haze * haze) / extinction;
            source += light * (phase * visibility * fog_light_transmittance(above * slant));
        }
        source *= sky_ubo.fog_albedo.rgb;

        // exact over the step for a constant source
        float step_transmittance = exp(-extinction * dt);
        float weight = transmittance * (1.0 - step_transmittance);
        scatter += source * weight;
        distance_sum += t * weight;
        weight_sum += weight;

        transmittance *= step_transmittance;
        if (transmittance < 0.003)
        {
            transmittance = 0.0;
            break;
        }
    }

    if (weight_sum > 1e-6)
        fog_distance = distance_sum / weight_sum;
    return vec4(scatter, transmittance);
}

#endif // SKY_FOG
