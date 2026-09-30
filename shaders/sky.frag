#version 450

// Sky behind the scene (SkyRenderer, pass "Sky"), where nothing was drawn: the atmosphere and the clouds
// marched at low resolution (sky_march.comp), and what needs the full one between them - sun, moon, stars.

#include "resources/camera.glsl"
#include "resources/gbuffer.glsl"
#include "resources/sky.glsl"
#include "resources/sky_buffers.glsl"
#include "sky/view_ray.glsl"
#include "sky/atmosphere.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    if (get_gbuffer_DEPTH(v_uv).r < 1.0)
        discard;

    vec3 dir = sky_view_ray(v_uv);
    float world_y = camera_ubo.camera_pos.y;

    vec4 atmosphere;
    if (!sky_buffer_sample(u_atmosphere, v_uv, atmosphere))
    {
        // (every sky pixel has a marched texel; should one be missing, the exact value)
        vec3 transmittance;
        atmosphere.rgb = sky_atmosphere(world_y, dir, transmittance);
        atmosphere.a = sky_luminance(transmittance);
    }
    vec3 color = atmosphere.rgb + sky_space(world_y, dir, atmosphere.a, true);

    vec4 clouds;
    if (sky_buffer_sample(u_clouds, v_uv, clouds))
        color = color * clouds.a + clouds.rgb;

    out_color = vec4(color, 1.0);
}
