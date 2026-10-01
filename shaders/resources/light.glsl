#ifndef RESOURCES_LIGHT
#define RESOURCES_LIGHT

#ifndef SET_LIGHT
    #define SET_LIGHT 0 
    #error "SET_LIGHT definition is missing. Provide this resource: light"
#endif
#ifndef BINDING_UBO_LIGHT
    #define BINDING_UBO_LIGHT 0 
    #error "BINDING_UBO_LIGHT definition is missing. Provide this resource: light"
#endif

struct PointLight
{
    vec4 position;
    vec4 color;
};

#define SHADOW_CASCADES 4

// system_ubo.ixx
struct DirectionalLight
{
    mat4 cascade_vp[SHADOW_CASCADES];   // light view-projection of each cascade (ZO depth)
    vec4 direction;
    vec4 color;
    vec4 cascade_texel;                 // world size of a shadow texel, per cascade
    vec4 cascade_depth_range;           // m between the near and far plane, per cascade
    vec4 cascade_params;                // x: cascades in use, y: blend band (tile uv), z: 1 / atlas texels, w: tiles per side
    // shadow of the clouds, a map in light space: uv of p = (dot(p, u.xyz) + u.w, dot(p, v.xyz) + v.w)
    vec4 cloud_shadow_u;                // xyz: u axis (world, unit) / size of the map, w: offset
    vec4 cloud_shadow_v;                // the same for v
    vec4 cloud_shadow_params;           // x: strength (0: no cloud shadows), y: size of the map (m), z: dot(camera position, towards the light)
    vec4 shadow_softness;               // x: tan of the light's angular radius (0: fixed 3x3 PCF), y: largest penumbra radius (texels of a cascade)
};

layout(set = SET_LIGHT, binding = BINDING_UBO_LIGHT) 
uniform LightUBO
{
    PointLight lights[8];
    int light_count;

    DirectionalLight dir_light;
    int has_dir_light;

} light_ubo;


#endif // RESOURCES_LIGHT