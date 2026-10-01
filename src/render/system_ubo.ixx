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

// Cascaded shadow map of the directional light: the cascades are tiles of one depth atlas
// (GenericRenderGraph::build_shadow_cascades, shaders/resources/shadow.glsl)
export inline constexpr uint32_t shadow_cascade_count = 4;

export struct DirectionalLight
{
    glm::mat4 cascade_vp[shadow_cascade_count];  // light view-projection of each cascade (ZO depth)
    glm::vec4 direction;            // xyz normalized (world)
    glm::vec4 color;                // rgb * intensity
    glm::vec4 cascade_texel;        // world size of a shadow texel, per cascade
    glm::vec4 cascade_depth_range;  // m between the near and far plane, per cascade
    glm::vec4 cascade_params;       // x: cascades in use, y: blend band (tile uv), z: 1 / atlas texels, w: tiles per side
    // Shadow of the clouds: a map in light space around the camera (game: SkyRenderer, cloud_shadow.frag).
    // uv of a world point p = (dot(p, u.xyz) + u.w, dot(p, v.xyz) + v.w)
    glm::vec4 cloud_shadow_u;       // xyz: u axis (world, unit) / size of the map, w: offset
    glm::vec4 cloud_shadow_v;       // the same for v
    glm::vec4 cloud_shadow_params;  // x: strength (0: no cloud shadows), y: size of the map (m), z: dot(camera position, towards the light)
    // Penumbra of the cascades (shadow.glsl, PCSS): it widens with the distance from the blocker to the receiver
    glm::vec4 shadow_softness;      // x: tan of the light's angular radius (0: fixed 3x3 PCF), y: largest penumbra radius (texels of a cascade)
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


// Sky of the frame: atmosphere, celestial bodies, cloud layer (game: SkyRenderer, shaders/resources/sky.glsl).
// Distances in meters, coefficients in 1 / m.
export struct SkyUBO
{
    glm::vec4 sun_direction;         // xyz: towards the sun (atmosphere, sun disc), w: cos of the disc's angular radius
    glm::vec4 sun_illuminance;       // rgb: sun light above the atmosphere, w: disc radiance per unit of it
    glm::vec4 moon_direction;        // xyz: towards the moon (zero: none), w: cos of the disc's angular radius
    glm::vec4 moon_radiance;         // rgb: radiance of the moon disc
    glm::vec4 light_direction;       // xyz: towards the body which lights the clouds (sun or moon)
    glm::vec4 light_color;           // rgb: its light above them
    glm::vec4 rayleigh_scattering;   // rgb: at the ground, w: scale height
    glm::vec4 mie_scattering;        // rgb: at the ground, w: scale height
    glm::vec4 mie_extinction;        // rgb: at the ground, w: anisotropy g
    glm::vec4 ozone_absorption;      // rgb: at the center of the ozone layer, w: multiple scattering factor
    glm::vec4 planet;                // x: ground radius, y: atmosphere radius, z: altitude of world y = 0, w: sky intensity
    glm::vec4 ground_albedo;         // rgb
    glm::vec4 night_radiance;        // rgb: night sky, w: stars intensity
    glm::vec4 stars_rotation;        // xyz: axis, w: angle (rad)
    glm::vec4 ambient_top;           // rgb: sky light on the clouds (average sky radiance)
    glm::vec4 ambient_bottom;        // rgb: light from the ground below them
    glm::vec4 cloud_layer;           // x: bottom (m above world y = 0), y: thickness, z: coverage threshold, w: extinction at full density
    glm::vec4 cloud_scale;           // x: 1 / shape noise size, y: 1 / detail noise size, z: 1 / weather cell size, w: detail strength
    glm::vec4 cloud_offset;          // xyz: wind offset of the shapes (m), w: weather amplitude (threshold units)
    glm::vec4 cloud_detail_offset;   // xyz: offset of the detail noise (m), w: 1 when the level has clouds
    glm::vec4 cloud_albedo;          // rgb, w: ambient strength
    glm::vec4 cloud_phase;           // x: forward anisotropy, y: backward anisotropy, z: weight of the backward lobe, w: powder
    glm::vec4 cloud_march;           // x: max distance, y: time (s)
    // fog (shaders/sky/fog.glsl): a layer in the low ground and a haze everywhere
    glm::vec4 fog_layer;             // x: extinction of the ground fog at and below its base, y: base (world y), z: 1 / falloff height, w: extinction of the haze at world y = 0
    glm::vec4 fog_haze;              // x: 1 / scale height of the haze, y: max distance, z: ambient strength, w: share of the light the clouds let through on average
    glm::vec4 fog_albedo;            // rgb, w: forward anisotropy
    glm::vec4 fog_noise;             // x: 1 / size of the wisps, y: their strength, z: how far they lift the layer (m), w: scale of the directional light in the haze
    glm::vec4 fog_offset;            // xyz: wind offset of the wisps (m), w: 1 when there is fog to draw
};
RH_REGISTER_TYPE(SkyUBO)

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
    glm::uvec4 info;         // x: slots to scan, y: sky probe slot + 1 (0: none), z: specular mips, w: parallax on
    glm::vec4 sky_ambient;   // rgb: radiance used while nothing is baked, w: sky probe distance
    glm::vec4 intensity;     // x: diffuse, y: specular (0: off)
    glm::vec4 ssr_params;    // x: SSR intensity (0: off) - ssr_composite.frag
};
RH_REGISTER_TYPE(ReflectionProbesUBO)

export struct ProbeCaptureUBO
{
    glm::mat4 face_view_proj[6];  // cube face order +X -X +Y -Y +Z -Z (Vulkan cubemap layers)
    glm::vec4 capture_position;   // xyz, w: unused
    glm::vec4 filter_params;      // x: source face size, y: source mip count
    // lights baked into the probe (Light::visible_in_reflection_probes): nearest point lights to the capture
    // point, the sun (light_ubo.dir_light, with the shadow map) only if it is visible to probes
    glm::vec4 light_position[8];  // xyz
    glm::vec4 light_color[8];     // rgb
    glm::uvec4 light_info;        // x: point light count, y: sun visible
};
RH_REGISTER_TYPE(ProbeCaptureUBO)
