#ifndef UTILS_VOXEL_GRID
#define UTILS_VOXEL_GRID

// GPU layout of a sparse voxel grid (src/voxel/voxel_gpu.ixx, voxel::GpuGridData): change both together.
// Structs and index math only; the resources that hold the buffers include this file.
//
//   header      GpuGridHeader
//   bricks      GpuBrick[brick count]
//   voxels      uint[brick count * 512]: brick i at i * 512, x + y * 8 + z * 64 inside it
//   brick_map   uint[map_size.x * map_size.y * map_size.z]: brick coordinate -> pool index, VOXEL_EMPTY_BRICK
//
// A voxel is r | g << 8 | b << 16 | density << 24: unpackUnorm4x8 gives sRGB albedo in xyz, density in w.

const int VOXEL_BRICK_SIZE = 8;
const int VOXEL_BRICK_SHIFT = 3;
const uint VOXEL_BRICK_VOXELS = 512u;
const uint VOXEL_EMPTY_BRICK = 0xffffffffu;
const uint VOXEL_SOLID_DENSITY = 128u;

const uint VOXEL_BRICK_DETACHED = 1u;

struct GpuGridHeader
{
    vec4 origin_voxel_size;     // xyz: origin (object local space), w: voxel size (m)
    ivec4 map_min;              // xyz: brick coordinate of brick_map[0], w: brick count
    ivec4 map_size;             // xyz: brick_map dimensions (bricks)
};

struct GpuBrick
{
    ivec3 coord;
    uint meta;                  // solid count (bits 0..9) | generation << 10 | flags << 18
};

uint voxel_brick_solid_count(GpuBrick brick) { return brick.meta & 0x3ffu; }
uint voxel_brick_generation(GpuBrick brick) { return (brick.meta >> 10) & 0xffu; }
uint voxel_brick_flags(GpuBrick brick) { return (brick.meta >> 18) & 0xffu; }

ivec3 voxel_brick_of(ivec3 voxel) { return voxel >> VOXEL_BRICK_SHIFT; }
ivec3 voxel_local_of(ivec3 voxel) { return voxel & (VOXEL_BRICK_SIZE - 1); }

uint voxel_local_index(ivec3 local)
{
    return uint(local.x) | uint(local.y) << VOXEL_BRICK_SHIFT | uint(local.z) << (2 * VOXEL_BRICK_SHIFT);
}

ivec3 voxel_local_coord(uint index)
{
    return ivec3(index & 7u, (index >> VOXEL_BRICK_SHIFT) & 7u, index >> (2 * VOXEL_BRICK_SHIFT));
}

// Index into brick_map, VOXEL_EMPTY_BRICK outside it
uint voxel_brick_map_index(GpuGridHeader header, ivec3 brick)
{
    ivec3 p = brick - header.map_min.xyz;
    if (any(lessThan(p, ivec3(0))) || any(greaterThanEqual(p, header.map_size.xyz)))
        return VOXEL_EMPTY_BRICK;
    return uint((p.z * header.map_size.y + p.y) * header.map_size.x + p.x);
}

// Index of the voxel in the pool, given its brick's pool index
uint voxel_pool_index(uint brick_index, ivec3 voxel)
{
    return brick_index * VOXEL_BRICK_VOXELS + voxel_local_index(voxel_local_of(voxel));
}

bool voxel_is_solid(uint bits) { return (bits >> 24) >= VOXEL_SOLID_DENSITY; }
float voxel_density(uint bits) { return float(bits >> 24) / 255.0; }
vec3 voxel_albedo_srgb(uint bits) { return unpackUnorm4x8(bits).rgb; }

// Object local space
vec3 voxel_center(GpuGridHeader header, ivec3 voxel)
{
    return header.origin_voxel_size.xyz + (vec3(voxel) + 0.5) * header.origin_voxel_size.w;
}

ivec3 voxel_at(GpuGridHeader header, vec3 local_position)
{
    return ivec3(floor((local_position - header.origin_voxel_size.xyz) / header.origin_voxel_size.w));
}

#endif // UTILS_VOXEL_GRID
