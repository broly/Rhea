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

#ifndef BINDING_SAMPLER_CLOUD_SHADOW
    #define BINDING_SAMPLER_CLOUD_SHADOW 0
    #error "BINDING_SAMPLER_CLOUD_SHADOW definition is missing. Provide this resource: shadow"
#endif

layout(set = SET_SHADOW_RESOURCE, binding = BINDING_UBO_SHADOW)
uniform sampler2DShadow u_shadow_depth;

layout(set = SET_SHADOW_RESOURCE, binding = BINDING_UBO_SHADOW_DEBUG)
uniform sampler2D u_shadow_depth_debug;

// r: share of the directional light the clouds let through (cloud_shadow.frag)
layout(set = SET_SHADOW_RESOURCE, binding = BINDING_SAMPLER_CLOUD_SHADOW)
uniform sampler2D u_cloud_shadow;

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

// Light space position of a world point in a cascade: xy the tile's uv, z its depth (0..1)
vec3 cascade_project(int cascade, vec3 p)
{
    vec4 clip = light_ubo.dir_light.cascade_vp[cascade] * vec4(p, 1.0);
    vec3 proj = clip.xyz / clip.w;
    return vec3(proj.xy * 0.5 + 0.5, proj.z);
}

// Point i of n of a Vogel (golden angle) disc of radius 1, turned by `angle`
vec2 vogel_disc(int i, int n, float angle)
{
    float r = sqrt((float(i) + 0.5) / float(n));
    float theta = float(i) * 2.39996323 + angle;
    return r * vec2(cos(theta), sin(theta));
}

const int SHADOW_BLOCKER_TAPS = 8;
const int SHADOW_PCF_TAPS = 16;

// Shadow of one cascade (1 lit), -1 when the point is outside of it. edge: distance to the tile border (uv)
//
// Soft (light_ubo.dir_light.shadow_softness.x > 0): percentage closer soft shadows. The blockers around the
// point give their average distance to it along the light, the penumbra is that distance x tan(angular
// radius of the sun) - a few cm under a character, tens of metres for a mountain kilometres away - and the
// comparison filter is that wide. A wide filter samples the receiver itself far from the point: its depth there
// is extrapolated along its plane (receiver plane depth bias), or a slope lit at a grazing angle (terrain at
// sunset) shadows itself.
float cascade_shadow(int cascade, vec3 world_pos, vec3 Ng, out float edge)
{
    edge = 0.0;
    float texel = light_ubo.dir_light.cascade_texel[cascade];
    float depth_range = light_ubo.dir_light.cascade_depth_range[cascade];
    float ndotl = clamp(dot(Ng, -light_ubo.dir_light.direction.xyz), 0.0, 1.0);
    // normal offset in texels of this cascade, more at grazing angles (acne)
    vec3 p = world_pos + Ng * texel * (1.0 + 2.0 * (1.0 - ndotl));

    vec3 proj = cascade_project(cascade, p);
    vec2 uv = proj.xy;

    float tiles = light_ubo.dir_light.cascade_params.w;
    float atlas_texel = light_ubo.dir_light.cascade_params.z;
    float tan_light = light_ubo.dir_light.shadow_softness.x;
    float max_radius = tan_light > 0.0 ? light_ubo.dir_light.shadow_softness.y : 1.0;   // texels
    edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    // the filter stays inside the tile
    if (edge < (max_radius + 1.0) * atlas_texel * tiles || proj.z < 0.0 || proj.z > 1.0)
        return -1.0;

    int tiles_per_side = int(tiles + 0.5);
    vec2 tile = vec2(float(cascade % tiles_per_side), float(cascade / tiles_per_side));
    vec2 atlas_uv = (uv + tile) / tiles;
    float depth = proj.z - (0.01 + 0.5 * texel) / depth_range;

    if (tan_light <= 0.0)
    {
        float lit = 0.0;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                lit += textureLod(u_shadow_depth, vec3(atlas_uv + vec2(x, y) * atlas_texel, depth), 0.0);
        return lit / 9.0;
    }

    // ---- the receiver's plane in light space: d depth / d uv of the tile ----
    vec3 t1 = normalize(cross(Ng, abs(Ng.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 t2 = cross(Ng, t1);
    vec3 d1 = cascade_project(cascade, p + t1) - proj;
    vec3 d2 = cascade_project(cascade, p + t2) - proj;
    float det = d1.x * d2.y - d2.x * d1.y;
    vec2 depth_gradient = abs(det) > 1e-12
        ? vec2(d1.z * d2.y - d2.z * d1.y, d1.x * d2.z - d2.x * d1.z) / det
        : vec2(0.0);
    // steeper than ~80 degrees to the light: NdotL hardly lights it anyway, the extrapolation would run away
    float max_gradient = 6.0 * texel / depth_range / (atlas_texel * tiles);
    float gradient_length = length(depth_gradient);
    if (gradient_length > max_gradient)
        depth_gradient *= max_gradient / gradient_length;
    // per atlas texel of offset
    depth_gradient *= atlas_texel * tiles;

    // interleaved gradient noise turns the discs from pixel to pixel: noise instead of copies of the edge
    float angle = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    // ---- blockers: within the widest penumbra a blocker this far up the light could cast ----
    float search_radius = clamp(proj.z * depth_range * tan_light / texel, 1.0, max_radius);
    float blocker_sum = 0.0;
    int blockers = 0;
    for (int i = 0; i < SHADOW_BLOCKER_TAPS; ++i)
    {
        vec2 offset = vogel_disc(i, SHADOW_BLOCKER_TAPS, angle) * search_radius;
        float stored = textureLod(u_shadow_depth_debug, atlas_uv + offset * atlas_texel, 0.0).r;
        if (stored < depth + dot(depth_gradient, offset))
        {
            blocker_sum += stored;
            blockers++;
        }
    }
    if (blockers == 0)
        return 1.0;

    // ---- the comparison filter as wide as the penumbra ----
    float blocker_distance = max(depth - blocker_sum / float(blockers), 0.0) * depth_range;   // m
    float radius = clamp(blocker_distance * tan_light / texel, 1.0, max_radius);
    float lit = 0.0;
    for (int i = 0; i < SHADOW_PCF_TAPS; ++i)
    {
        vec2 offset = vogel_disc(i, SHADOW_PCF_TAPS, angle) * radius;
        lit += textureLod(u_shadow_depth, vec3(atlas_uv + offset * atlas_texel, depth + dot(depth_gradient, offset)), 0.0);
    }
    return lit / float(SHADOW_PCF_TAPS);
}

// Share of the directional light which reaches a point in the air (fog, sky/fog.glsl): one tap of the first
// cascade that contains it, no bias and no filter - the march which calls it averages many samples per ray.
// Lit beyond the cascades.
float cascade_shadow_volume(vec3 world_pos)
{
    int count = int(light_ubo.dir_light.cascade_params.x + 0.5);
    float tiles = light_ubo.dir_light.cascade_params.w;
    float atlas_texel = light_ubo.dir_light.cascade_params.z;
    int tiles_per_side = int(tiles + 0.5);
    for (int cascade = 0; cascade < count; ++cascade)
    {
        vec4 clip = light_ubo.dir_light.cascade_vp[cascade] * vec4(world_pos, 1.0);
        vec3 proj = clip.xyz / clip.w;
        vec2 uv = proj.xy * 0.5 + 0.5;
        float edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
        // the bilinear comparison stays inside the tile
        if (edge < atlas_texel * tiles || proj.z < 0.0 || proj.z > 1.0)
            continue;
        vec2 tile = vec2(float(cascade % tiles_per_side), float(cascade / tiles_per_side));
        return textureLod(u_shadow_depth, vec3((uv + tile) / tiles, proj.z), 0.0);
    }
    return 1.0;
}

// Shadow of the clouds (SkyRenderer, pass "CloudShadow"): the share of the directional light which gets through
// them to the point. A map in light space around the camera, every point on a ray of the light reads the same
// texel; lit beyond the map.
float cloud_shadow(vec3 world_pos)
{
    float strength = light_ubo.dir_light.cloud_shadow_params.x;
    if (strength <= 0.0)
        return 1.0;

    vec2 uv = vec2(dot(world_pos, light_ubo.dir_light.cloud_shadow_u.xyz) + light_ubo.dir_light.cloud_shadow_u.w,
                   dot(world_pos, light_ubo.dir_light.cloud_shadow_v.xyz) + light_ubo.dir_light.cloud_shadow_v.w);
    float edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    if (edge <= 0.0)
        return 1.0;
    float transmittance = textureLod(u_cloud_shadow, uv, 0.0).r;
    // fades out towards the border of the map
    return mix(1.0, transmittance, strength * clamp(edge * 16.0, 0.0, 1.0));
}

// Share of the directional light which reaches the point: the cascades (scene) and the clouds above it
float shadow_factor(vec3 world_pos, vec3 Ng)
{
    float clouds = cloud_shadow(world_pos);
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
        return shadow * clouds;
    }
    return clouds;
}


#endif // RESOURCES_SHADOW