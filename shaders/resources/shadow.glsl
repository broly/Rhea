#ifndef RESOURCES_SHADOW
#define RESOURCES_SHADOW

#ifndef SET_SHADOW_RESOURCE
    #define SET_SHADOW_RESOURCE 0
    #error "SET_SHADOW_RESOURCE definition is missing. Provide this resource: shadow"
#endif
#ifndef BINDING_UBO_SHADOW
    #define BINDING_UBO_SHADOW 0
    #error "BINDING_UBO_SHADOW definition is missing. Provide this resource: shadow"
#endif

#ifndef BINDING_UBO_SHADOW_DEBUG
    #define BINDING_UBO_SHADOW_DEBUG 0
    #error "BINDING_UBO_SHADOW_DEBUG definition is missing. Provide this resource: shadow"
#endif

layout(set = SET_SHADOW_RESOURCE, binding = BINDING_UBO_SHADOW)
uniform sampler2DShadow u_shadow_depth;

layout(set = SET_SHADOW_RESOURCE, binding = BINDING_UBO_SHADOW_DEBUG)
uniform sampler2D u_shadow_depth_debug;




float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

mat2 rot2(float a)
{
    float s = sin(a), c = cos(a);
    return mat2(c, -s, s, c);
}

// Cascaded shadow map (GenericRenderGraph::build_shadow_cascades): every cascade is a tile of one depth atlas.
// The first cascade that contains the point is used, blended into the next one near its border; points
// outside of all cascades (farther than the shadow distance) are lit. Works for any view: the cascade is
// found by position, not by view depth (probe captures sample it too).

// Shadow of one cascade (1 lit), -1 when the point is outside of it. edge: distance to the tile border (uv)
float cascade_shadow(int cascade, vec3 world_pos, vec3 Ng, out float edge)
{
    edge = 0.0;
    float texel = light_ubo.dir_light.cascade_texel[cascade];
    float ndotl = clamp(dot(Ng, -light_ubo.dir_light.direction.xyz), 0.0, 1.0);
    // normal offset in texels of this cascade, more at grazing angles (acne)
    vec3 p = world_pos + Ng * texel * (1.0 + 2.0 * (1.0 - ndotl));

    vec4 clip = light_ubo.dir_light.cascade_vp[cascade] * vec4(p, 1.0);
    vec3 proj = clip.xyz / clip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;

    float tiles = light_ubo.dir_light.cascade_params.w;
    float atlas_texel = light_ubo.dir_light.cascade_params.z;
    edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    // the 3x3 filter stays inside the tile
    if (edge < 2.0 * atlas_texel * tiles || proj.z < 0.0 || proj.z > 1.0)
        return -1.0;

    int tiles_per_side = int(tiles + 0.5);
    vec2 tile = vec2(float(cascade % tiles_per_side), float(cascade / tiles_per_side));
    vec2 atlas_uv = (uv + tile) / tiles;
    float depth = proj.z - (0.01 + 0.5 * texel) / light_ubo.dir_light.cascade_depth_range[cascade];

    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += textureLod(u_shadow_depth, vec3(atlas_uv + vec2(x, y) * atlas_texel, depth), 0.0);
    return lit / 9.0;
}

float shadow_factor(vec3 world_pos, vec3 Ng)
{
    int count = int(light_ubo.dir_light.cascade_params.x + 0.5);
    float band = light_ubo.dir_light.cascade_params.y;
    for (int cascade = 0; cascade < count; ++cascade)
    {
        float edge;
        float shadow = cascade_shadow(cascade, world_pos, Ng, edge);
        if (shadow < 0.0)
            continue;
        if (edge < band && cascade + 1 < count)
        {
            float next_edge;
            float next = cascade_shadow(cascade + 1, world_pos, Ng, next_edge);
            if (next >= 0.0)
                shadow = mix(next, shadow, edge / band);
        }
        return shadow;
    }
    return 1.0;
}


#endif // RESOURCES_SHADOW