export module render:system_ubo;

import std.compat;
import fixed_string;
import reflect;
import type_id;

import glm;
#include "common/reflect_macros.h"

export struct MaterialUBO
{
    float base_color_factor;
    float emissive_factor;
    float occlusion_factor;
    float roughness_factor;
    float metallic_factor;
};
RH_REGISTER_TYPE(MaterialUBO)

export struct PointLight
{
    glm::vec4 position;
    glm::vec4 color;
};

export struct DirectionalLight
{
    glm::mat4 light_vp;  // view-projection for shadow
    glm::vec4 direction; // xyz normalized (world)
    glm::vec4 color;     // rgb * intensity
};

export struct alignas(16) LightUBO
{
    PointLight lights[8];
    int light_count;
    [[=rh::padding]] glm::vec3 _pad0;
    
    DirectionalLight dir_light;
    int has_dir_light;
    [[=rh::padding]] glm::vec3 _pad1;
};
RH_REGISTER_TYPE(LightUBO)


export struct CloudsUBO
{
    glm::vec4 planet_center; // xyz = center, w = radius

    // x = base height
    // y = thickness
    // z = coverage
    // w = density
    glm::vec4 cloud_base;

    glm::vec4 sun_direction; // xyz normalized
    glm::vec4 sun_color;     // rgb * intensity

    // rgb = albedo
    // a   = extinction
    glm::vec4 cloud_color;

    // x = forward scattering (g ~ 0.6–0.8)
    // y = backward scattering
    // z = ambient scattering
    glm::vec4 scattering;

    // xyz = wind direction * speed
    // w   = time
    glm::vec4 wind;
    
    glm::vec4 sky_ambient;
    glm::vec4 horizon_color;
};
RH_REGISTER_TYPE(CloudsUBO)

// ---- reflection probes (game: ReflectionProbeSystem, shaders/resources/reflection.glsl) ----

export constexpr uint32_t kMaxReflectionProbes = 16;

export struct GPUReflectionProbe
{
    glm::vec4 capture_position;  // xyz: capture point, w: 1 once the probe holds a filtered capture
    glm::vec4 box_min;           // xyz: influence / parallax box, w: blend distance (fade inside the box)
    glm::vec4 box_max;           // xyz, w: intensity
};

export struct ReflectionProbesUBO
{
    GPUReflectionProbe probes[kMaxReflectionProbes];
    glm::uvec4 info;         // x: slots to scan, y: unused, z: specular mips, w: parallax on
    glm::vec4 sky_ambient;   // rgb: radiance used while no probe is baked
    glm::vec4 intensity;     // x: diffuse, y: specular (0: off)
    glm::vec4 ssr_params;    // x: SSR intensity (0: off) - ssr_composite.frag
};
RH_REGISTER_TYPE(ReflectionProbesUBO)

export struct ProbeCaptureUBO
{
    glm::mat4 face_view_proj[6];  // cube face order +X -X +Y -Y +Z -Z (Vulkan cubemap layers)
    glm::vec4 capture_position;   // xyz, w: unused
    glm::vec4 sky_color;          // rgb: sky radiance behind the clouds, a: cloud density scale
    glm::vec4 sky_ambient;        // rgb: ambient radiance of surfaces no probe covers yet
    glm::vec4 filter_params;      // x: source face size, y: source mip count
    // lights baked into the probe (Light::visible_in_reflection_probes): nearest point lights to the capture
    // point, the sun (light_ubo.dir_light, with the shadow map) only if it is visible to probes
    glm::vec4 light_position[8];  // xyz
    glm::vec4 light_color[8];     // rgb
    glm::uvec4 light_info;        // x: point light count, y: sun visible
};
RH_REGISTER_TYPE(ProbeCaptureUBO)
