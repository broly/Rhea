export module voxel:gpu;

import std.compat;
import glm;

import :grid;

// GPU layout of a VoxelGrid: what the renderer (V3) and the 3D JPEG (V4) read and write in storage buffers
// (std430). Mirrored in shaders/utils/voxel_grid.glsl: change both together.
//
//   header      one GpuGridHeader per grid
//   bricks      GpuBrick[brick count]: coordinate and state of brick i
//   voxels      uint[brick count * 512]: the voxels of brick i start at i * 512, local_index order (the pool)
//   brick_map   uint[map_size.x * map_size.y * map_size.z]: dense index over the bricks' bounding box, brick
//               (x, y, z) -> its index in the pool, empty_brick where there is none. A house of 8 x 6 x 6 m at
//               10 cm is 10 x 8 x 8 entries: neighbour lookups across bricks cost one read
export namespace voxel
{
    inline constexpr uint32_t empty_brick = 0xffffffffu;

    struct GpuGridHeader
    {
        glm::vec4 origin_voxel_size{ 0.0f };            // xyz: origin (object local space), w: voxel size (m)
        glm::ivec4 map_min{ 0 };                        // xyz: brick coordinate of brick_map[0], w: brick count
        glm::ivec4 map_size{ 0 };                       // xyz: brick_map dimensions (bricks), w: 0
    };
    static_assert(sizeof(GpuGridHeader) == 48);

    // meta: solid_count (bits 0..9, up to 512) | generation << 10 (8 bits) | flags << 18 (8 bits)
    struct GpuBrick
    {
        glm::ivec3 coord{ 0 };
        uint32_t meta = 0;
    };
    static_assert(sizeof(GpuBrick) == 16);

    constexpr uint32_t pack_brick_meta(const Brick& brick)
    {
        return uint32_t(brick.solid_count) | uint32_t(brick.generation) << 10 | uint32_t(brick.flags) << 18;
    }

    struct GpuGridData
    {
        GpuGridHeader header;
        std::vector<GpuBrick> bricks;
        std::vector<uint32_t> voxels;
        std::vector<uint32_t> brick_map;

        size_t bricks_bytes() const { return bricks.size() * sizeof(GpuBrick); }
        size_t voxels_bytes() const { return voxels.size() * sizeof(uint32_t); }
        size_t brick_map_bytes() const { return brick_map.size() * sizeof(uint32_t); }
    };

    // Everything for a first upload; brick i of the pool is brick i of the grid
    GpuGridData pack_for_gpu(const VoxelGrid& grid);

    // Byte offset of brick i's voxels in the pool: re-uploading one brick changed on the CPU
    constexpr size_t brick_voxels_offset(uint32_t brick_index)
    {
        return size_t(brick_index) * brick_voxels * sizeof(uint32_t);
    }

    // Index into brick_map of a brick coordinate, empty_brick outside the map
    uint32_t brick_map_index(const GpuGridHeader& header, glm::ivec3 brick);
}
