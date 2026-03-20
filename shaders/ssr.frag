#version 450

#include "resources/camera.glsl"
#include "resources/hdr_color_output.glsl"
#include "resources/gbuffer.glsl"


layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_ssr;

const int   MAX_STEPS   = 200;
const float MAX_DIST    = 100.0;
const float THICKNESS   = 0.002;
const float SELF_BIAS   = 0.05;

vec3 reconstructViewPos(vec2 uv, float depth)
{
    float z = depth * 2.0 - 1.0;
    vec4 clip = vec4(uv * 2.0 - 1.0, z, 1.0);
    vec4 view = inverse(camera_ubo.proj) * clip;
    return view.xyz / view.w;
}

void main()
{
    float depth = texture(u_depth, v_uv).r;
    if (depth >= 1.0)
    {
        out_ssr = vec4(0.0);
        return;
    }

    vec4 normalData = texture(u_normal, v_uv);
    vec3 normal = normalize(normalData.xyz * 2.0 - 1.0);
    float roughness = normalData.w;

    if (roughness > 0.8)
    {
        out_ssr = vec4(0.0);
        return;
    }

    vec3 viewPos = reconstructViewPos(v_uv, depth);
    vec3 viewDir = normalize(-viewPos);
    vec3 reflDir = normalize(reflect(viewDir, normal));

    vec3 startVS = viewPos + reflDir * SELF_BIAS;
    vec3 endVS   = viewPos + reflDir * MAX_DIST;

    // project start
    vec4 startClip = camera_ubo.proj * vec4(startVS, 1.0);
    vec3 startNDC  = startClip.xyz / startClip.w;

    // project end
    vec4 endClip = camera_ubo.proj * vec4(endVS, 1.0);
    vec3 endNDC  = endClip.xyz / endClip.w;

    vec2 startUV = startNDC.xy * 0.5 + 0.5;
    vec2 endUV   = endNDC.xy   * 0.5 + 0.5;

    vec2 deltaUV = endUV - startUV;

    float maxComp = max(abs(deltaUV.x), abs(deltaUV.y));
    if (maxComp < 1e-5)
    {
        out_ssr = vec4(0.0);
        return;
    }
    
    float steps = max(abs(deltaUV.x), abs(deltaUV.y)) * 1024.0;
    steps = clamp(steps, 1.0, float(MAX_STEPS));
    steps = min(steps, float(MAX_STEPS));

    vec2 stepUV = deltaUV / steps;
    vec2 uv = startUV;

    for (int i = 0; i < int(steps); i++)
    {
        uv += stepUV;

        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            break;

        float sceneDepth = texture(u_depth, uv).r;

        float t = float(i) / steps;
        float rayDepth = mix(startNDC.z, endNDC.z, t);
        rayDepth = rayDepth * 0.5 + 0.5;

        if (rayDepth > sceneDepth && rayDepth - sceneDepth < THICKNESS)
        {
            vec3 hitColor = texture(u_hdr_color[COLOR_OUTPUT_HDR_BASE], uv).rgb;

            float NdotV = max(dot(normal, viewDir), 0.0);
            float fresnel = pow(1.0 - NdotV, 5.0);
            float strength = (1.0 - roughness) * fresnel;

            out_ssr = vec4(hitColor * strength, 1.0);
            return;
        }
    }

    out_ssr = vec4(0.0);
}