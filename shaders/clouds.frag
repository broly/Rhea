#version 450

#include "definitions.glsl"
#include "math.glsl"
#include "resources/gbuffer.glsl"
#include "resources/camera.glsl"

// ================== INPUT ==================
layout(location = 0) in vec2 v_uv;

// ================== OUTPUT =================
layout(location = 0) out vec4 out_color;

// ===================== CLOUDS ====================
layout(set = SET_CLOUDS, binding = BINDING_UBO_CLOUDS) uniform CloudsUBO
{
    vec4 planet_center; // xyz = center, w = radius
    
    // x = base height
    // y = thickness
    // z = coverage
    // w = density
    vec4 cloud_base;
    
    vec4 sun_direction; // xyz normalized
    vec4 sun_color;     // rgb * intensity
    
    // rgb = albedo
    // a   = extinction
    vec4 cloud_color;
    
    // x = forward scattering (g ~ 0.6–0.8)
    // y = backward scattering
    // z = ambient scattering
    vec4 scattering;
    
    // xyz = wind direction * speed
    // w   = time
    vec4 wind;
    
    vec4 sky_ambient;
    vec4 horizon_color;
} clouds_ubo;

// ================== TEXTURES =================
layout(set = SET_CLOUDS, binding = BINDING_SAMPLER_NOISE) uniform sampler2D u_noise;



// ================== HELPERS =================
float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec3 get_view_ray(vec2 uv)
{
    vec2 ndc = uv * 2.0 - 1.0;

    vec4 clip = vec4(ndc, 1.0, 1.0);
    vec4 view = inverse(camera_ubo.proj) * clip;
    view /= view.w;

    vec3 ray_view = normalize(view.xyz);
    mat3 inv_view_rot = mat3(transpose(camera_ubo.view));

    return normalize(inv_view_rot * ray_view);
}


vec3 sample_sky(vec3 ray_dir)
{
    vec3 sun_dir = normalize(clouds_ubo.sun_direction.xyz);

    float t = saturate(ray_dir.y * 0.5 + 0.5);
    t = smoothstep(0.0, 1.0, t);

    vec3 sky =
    mix(clouds_ubo.horizon_color.rgb, clouds_ubo.sky_ambient.rgb, t);

    float sun_dot = max(dot(ray_dir, sun_dir), 0.0);

    float halo = pow(sun_dot, 16.0) * 0.5;
    float core = pow(sun_dot, 128.0);

    vec3 sun =
    clouds_ubo.sun_color.rgb *
    clouds_ubo.sun_color.w *
    (halo + core);

    return sky + sun;
}

float sample_cloud_density(vec3 world_pos, vec3 ray_dir)
{
    float height = (world_pos.y - clouds_ubo.cloud_base.x) / (clouds_ubo.cloud_base.y - clouds_ubo.cloud_base.x);

    if (height < 0.0 || height > 1.0)
    return 0.0;

    vec2 wind_uv = world_pos.xz * 0.00025 + clouds_ubo.wind.xy * clouds_ubo.wind.w;

    float noise = texture(u_noise, wind_uv).r;
    noise = smoothstep(clouds_ubo.cloud_base.z, 1.0, noise);

    float height_fade = saturate(height * (1.0 - height) * 4.0);

    return noise * height_fade * clouds_ubo.cloud_base.w;
}


void main()
{
    float depth = get_gbuffer_DEPTH(v_uv).r;

    if (depth < 0.9999)
        discard;

    vec3 ray_dir = get_view_ray(v_uv);
    vec3 cam_pos = camera_ubo.camera_pos.xyz;

    float cloud_bottom = 100.0;
    float cloud_top    = 300.0;
    int   STEPS        = 64;
    float step_size    = (cloud_top - cloud_bottom) / float(STEPS);

    vec3 cloud_color = vec3(1.0);
    vec3 sky_color   = vec3(0.1, 0.5, 1.0);

    float alpha = 0.0;
    vec3 color = sky_color;

    for (int i = 0; i < STEPS; i++)
    {
        float t = cloud_bottom + float(i) * step_size;
        vec3 p = cam_pos + ray_dir * t;

        vec2 uv = p.xz * 0.001;
        float n = texture(u_noise, uv).r;

        float density = saturate(n - 0.4) * 0.5;

        if (density > 0.001)
        {
            float a = density * 0.08;

            color = mix(color, cloud_color, a);
            alpha += a;

            if (alpha > 0.95)
                break;
        }
    }

    out_color = vec4(color, 1.0);
}