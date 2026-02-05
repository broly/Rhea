export module render:system_ubo;

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
REFLECT_STRUCT_RUNTIME(MaterialUBO,
    base_color_factor, emissive_factor, occlusion_factor, roughness_factor, metallic_factor);

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

export struct LightUBO
{
    PointLight lights[8];
    int light_count;
    glm::vec3 _pad0;
    
    DirectionalLight dir_light;
    int has_dir_light;
    glm::vec3 _pad1;
};
REFLECT_STRUCT_RUNTIME_OPAQUE(LightUBO);



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
REFLECT_STRUCT_RUNTIME(CloudsUBO,
    planet_center, cloud_base, sun_direction, sun_color, cloud_color, scattering, wind);