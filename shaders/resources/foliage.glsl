#ifndef RESOURCES_FOLIAGE
#define RESOURCES_FOLIAGE

// Ground foliage of the terrain (FoliageRenderer, src/game/foliage_renderer.*; settings: TerrainFoliage, module
// foliage). Resource "foliage": written by the CPU every frame (copy per frame in flight).

#ifndef SET_FOLIAGE
    #define SET_FOLIAGE 0
    #error "SET_FOLIAGE definition is missing. Provide this resource: foliage"
#endif
#ifndef BINDING_SSBO_FOLIAGE_TABLE
    #define BINDING_SSBO_FOLIAGE_TABLE 0
    #error "BINDING_SSBO_FOLIAGE_TABLE definition is missing. Provide this resource: foliage"
#endif
#ifndef BINDING_SSBO_FOLIAGE_BUCKETS
    #define BINDING_SSBO_FOLIAGE_BUCKETS 0
    #error "BINDING_SSBO_FOLIAGE_BUCKETS definition is missing. Provide this resource: foliage"
#endif
#ifndef BINDING_SSBO_FOLIAGE_HEIGHTS
    #define BINDING_SSBO_FOLIAGE_HEIGHTS 0
    #error "BINDING_SSBO_FOLIAGE_HEIGHTS definition is missing. Provide this resource: foliage"
#endif
#ifndef BINDING_SAMPLER_FOLIAGE_MASK
    #define BINDING_SAMPLER_FOLIAGE_MASK 0
    #error "BINDING_SAMPLER_FOLIAGE_MASK definition is missing. Provide this resource: foliage"
#endif

const uint FOLIAGE_LODS = 3u;
const uint FOLIAGE_MAX_INTERACTORS = 4u;

// FoliageGlobals (src/game/foliage_renderer.ixx: same layout)
struct FoliageGlobals
{
    vec4 terrain;           // xy: world xz of sample (0, 0), z: spacing (m), w: samples per side
    vec4 terrain_info;      // x: world y of height 0, y: size (m), z: time (s), w: time of the last frame
    vec4 frustum[6];        // planes of the camera, dot(xyz, p) + w >= 0 inside
    vec4 camera;            // xyz: position
    vec4 wind;              // xy: direction (x, z), z: strength, w: gust strength
    vec4 gust;              // x: gust speed (m/s), y: 1 / gust scale (1 / m)
    uvec4 hzb;              // x, y: level 0 size, z: levels, w: 1 when the hierarchical depth of this frame is there
    uvec4 counts;           // x: types, y: interactors, z: buckets
    vec4 interactors[FOLIAGE_MAX_INTERACTORS];  // xyz: feet, w: radius
};

// GPUFoliageType (src/game/foliage_renderer.ixx: same layout)
struct FoliageType
{
    vec4 placement;         // x: variants, y: channel, z: density (per m2), w: grid spacing (m)
    vec4 fade;              // x: fade start, y: fade end, z: lod 1 distance, w: lod 2 distance
    vec4 mask;              // x: threshold, y: patch scale (m, 0: none), z: patch coverage, w: seed
    vec4 size;              // x: scale min, y: scale max, z: sink (m), w: ground align
    vec4 bounds;            // x: radius, y: height of the largest variant (m, scale 1), z: wind, w: transmission
    vec4 tint;              // rgb: tint, a: variation
    vec4 dry;               // rgb: dry color, a: dry fraction
    vec4 color;             // x: large scale color noise
    uvec4 buckets;          // x: first bucket (variant v, lod l: x + v * FOLIAGE_LODS + l)
};

// GPUFoliageBucket: the instances of one variant of a type at one LOD, drawn by one indexed indirect draw
struct FoliageBucket
{
    uvec4 draw;             // x: index count, y: first index, z: first instance, w: capacity
    uvec4 mesh;             // x: mesh (mesh table), y: material, z: type
    vec4 size;              // x: height of the variant (m, scale 1)
};

layout(std430, set = SET_FOLIAGE, binding = BINDING_SSBO_FOLIAGE_TABLE)
readonly buffer FoliageTable
{
    FoliageGlobals globals;
    FoliageType types[];
} u_foliage;

layout(std430, set = SET_FOLIAGE, binding = BINDING_SSBO_FOLIAGE_BUCKETS)
readonly buffer FoliageBuckets
{
    FoliageBucket buckets[];
} u_foliage_buckets;

// heights of the terrain samples, relative to terrain_info.x, rows along x
layout(std430, set = SET_FOLIAGE, binding = BINDING_SSBO_FOLIAGE_HEIGHTS)
readonly buffer FoliageHeights
{
    float heights[];
} u_foliage_heights;

// terrain foliage map: RGBA = densities of the channels, uv 0..1 over the terrain
layout(set = SET_FOLIAGE, binding = BINDING_SAMPLER_FOLIAGE_MASK) uniform sampler2D u_foliage_mask;

// ---- shared helpers ----

uint foliage_hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// [0, 1)
float foliage_random(inout uint state)
{
    state = foliage_hash(state + 0x9e3779b9u);
    return float(state >> 8) * (1.0 / 16777216.0);
}

float foliage_value_noise(vec2 p)
{
    const vec2 i = floor(p);
    const vec2 f = fract(p);
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const ivec2 c = ivec2(i);
    #define FOLIAGE_CORNER(ox, oy) (float(foliage_hash(uint(c.x + ox) * 0x8da6b343u ^ foliage_hash(uint(c.y + oy) * 0xd8163841u)) >> 8) * (1.0 / 16777216.0))
    const float a = FOLIAGE_CORNER(0, 0);
    const float b = FOLIAGE_CORNER(1, 0);
    const float c2 = FOLIAGE_CORNER(0, 1);
    const float d = FOLIAGE_CORNER(1, 1);
    #undef FOLIAGE_CORNER
    return mix(mix(a, b, u.x), mix(c2, d, u.x), u.y);
}

// ~[0, 1], two octaves
float foliage_noise(vec2 p)
{
    return foliage_value_noise(p) * 0.65 + foliage_value_noise(p * 2.13 + 17.3) * 0.35;
}

#endif // RESOURCES_FOLIAGE
