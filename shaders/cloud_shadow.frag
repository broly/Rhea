#version 450

// Shadow of the cloud layer on everything below it (SkyRenderer, pass "CloudShadow"): a map in the space of
// the directional light, around the camera. A texel is a ray of that light; it holds the share of the light
// the clouds let through along it. resources/shadow.glsl reads the map for every lit point (cloud_shadow):
// the projection of the map is in the light UBO, the same for the pass which writes it and those which read.

#include "resources/light.glsl"
#include "resources/sky.glsl"
#include "sky/clouds.glsl"

layout(push_constant) uniform CloudShadowPushConstants
{
    uint steps;     // samples through the cloud layer per texel
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out float out_transmittance;

void main()
{
    vec4 axis_u = light_ubo.dir_light.cloud_shadow_u;
    vec4 axis_v = light_ubo.dir_light.cloud_shadow_v;
    float size = light_ubo.dir_light.cloud_shadow_params.y;
    if (light_ubo.has_dir_light != 1 || light_ubo.dir_light.cloud_shadow_params.x <= 0.0 || size <= 0.0)
    {
        out_transmittance = 1.0;
        return;
    }

    // the point of the texel's ray on the plane through the camera, across the light
    vec3 light_dir = normalize(-light_ubo.dir_light.direction.xyz);
    vec3 origin = axis_u.xyz * ((v_uv.x - axis_u.w) * size * size)
        + axis_v.xyz * ((v_uv.y - axis_v.w) * size * size)
        + light_dir * light_ubo.dir_light.cloud_shadow_params.z;

    out_transmittance = cloud_shadow_transmittance(origin, light_dir, int(pc.steps));
}
