#ifndef RESOURCES_SKY
#define RESOURCES_SKY

// Sky of the frame (game: SkyRenderer, src/game/sky_renderer.cpp): the atmosphere, the celestial bodies and
// the cloud layer as the SkyAtmosphere / VolumetricClouds components of the level describe them, and the
// noise volumes of the clouds, and the VolumetricFog. Read by sky/atmosphere.glsl, sky/clouds.glsl and sky/fog.glsl.

#ifndef SET_SKY
    #define SET_SKY 0
    #error "SET_SKY definition is missing. Provide this resource: sky"
#endif
#ifndef BINDING_UBO_SKY
    #define BINDING_UBO_SKY 0
    #error "BINDING_UBO_SKY definition is missing. Provide this resource: sky"
#endif
#ifndef BINDING_SAMPLER_CLOUD_SHAPE
    #define BINDING_SAMPLER_CLOUD_SHAPE 0
    #error "BINDING_SAMPLER_CLOUD_SHAPE definition is missing. Provide this resource: sky"
#endif
#ifndef BINDING_SAMPLER_CLOUD_DETAIL
    #define BINDING_SAMPLER_CLOUD_DETAIL 0
    #error "BINDING_SAMPLER_CLOUD_DETAIL definition is missing. Provide this resource: sky"
#endif

// SkyUBO (system_ubo.ixx). Distances in meters, coefficients in 1 / m.
layout(set = SET_SKY, binding = BINDING_UBO_SKY)
uniform SkyUBO
{
    vec4 sun_direction;         // xyz: towards the sun (atmosphere, sun disc), w: cos of the disc's angular radius
    vec4 sun_illuminance;       // rgb: sun light above the atmosphere, w: disc radiance per unit of it
    vec4 moon_direction;        // xyz: towards the moon (zero: none), w: cos of the disc's angular radius
    vec4 moon_radiance;         // rgb: radiance of the moon disc
    vec4 light_direction;       // xyz: towards the body which lights the clouds (sun or moon)
    vec4 light_color;           // rgb: its light above them
    vec4 rayleigh_scattering;   // rgb: at the ground, w: scale height
    vec4 mie_scattering;        // rgb: at the ground, w: scale height
    vec4 mie_extinction;        // rgb: at the ground, w: anisotropy g
    vec4 ozone_absorption;      // rgb: at the center of the ozone layer, w: multiple scattering factor
    vec4 planet;                // x: ground radius, y: atmosphere radius, z: altitude of world y = 0, w: sky intensity
    vec4 ground_albedo;         // rgb
    vec4 night_radiance;        // rgb: night sky, w: stars intensity
    vec4 stars_rotation;        // xyz: axis, w: angle (rad)
    vec4 ambient_top;           // rgb: sky light on the clouds (average sky radiance)
    vec4 ambient_bottom;        // rgb: light from the ground below them
    vec4 cloud_layer;           // x: bottom (m above world y = 0), y: thickness, z: coverage threshold, w: extinction at full density
    vec4 cloud_scale;           // x: 1 / shape noise size, y: 1 / detail noise size, z: 1 / weather cell size, w: detail strength
    vec4 cloud_offset;          // xyz: wind offset of the shapes (m), w: weather amplitude (threshold units)
    vec4 cloud_detail_offset;   // xyz: offset of the detail noise (m), w: 1 when the level has clouds
    vec4 cloud_albedo;          // rgb, w: ambient strength
    vec4 cloud_phase;           // x: forward anisotropy, y: backward anisotropy, z: weight of the backward lobe, w: powder
    vec4 cloud_march;           // x: max distance, y: time (s)
    // fog (sky/fog.glsl): a layer in the low ground and a haze everywhere
    vec4 fog_layer;             // x: extinction of the ground fog at and below its base, y: base (world y), z: 1 / falloff height, w: extinction of the haze at world y = 0
    vec4 fog_haze;              // x: 1 / scale height of the haze, y: max distance, z: ambient strength, w: share of the light the clouds let through on average
    vec4 fog_albedo;            // rgb, w: forward anisotropy
    vec4 fog_noise;             // x: 1 / size of the wisps, y: their strength, z: how far they lift the layer (m), w: scale of the directional light in the haze
    vec4 fog_offset;            // xyz: wind offset of the wisps (m), w: 1 when there is fog to draw
} sky_ubo;

// tiling noise volumes (tools/sky/generate_cloud_noise.py)
// R: Perlin-Worley (shapes), GBA: Worley fbm at rising frequencies
layout(set = SET_SKY, binding = BINDING_SAMPLER_CLOUD_SHAPE)
uniform sampler3D u_cloud_shape;

// RGB: Worley fbm at rising frequencies
layout(set = SET_SKY, binding = BINDING_SAMPLER_CLOUD_DETAIL)
uniform sampler3D u_cloud_detail;

#endif // RESOURCES_SKY
