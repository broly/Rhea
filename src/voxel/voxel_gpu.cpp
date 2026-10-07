module voxel;

import std.compat;
import glm;

import :grid;
import :gpu;

namespace voxel
{
    uint32_t brick_map_index(const GpuGridHeader& header, glm::ivec3 brick)
    {
        const glm::ivec3 p = brick - glm::ivec3(header.map_min);
        const glm::ivec3 size = glm::ivec3(header.map_size);
        if (p.x < 0 || p.y < 0 || p.z < 0 || p.x >= size.x || p.y >= size.y || p.z >= size.z)
            return empty_brick;
        return uint32_t((p.z * size.y + p.y) * size.x + p.x);
    }

    GpuGridData pack_for_gpu(const VoxelGrid& grid)
    {
        GpuGridData data;
        const GridBounds bounds = grid.get_brick_bounds();
        const glm::ivec3 map_size = bounds.size();

        data.header.origin_voxel_size = glm::vec4(grid.get_origin(), grid.get_voxel_size());
        data.header.map_min = glm::ivec4(bounds.is_empty() ? glm::ivec3(0) : bounds.min, int32_t(grid.get_brick_count()));
        data.header.map_size = glm::ivec4(map_size, 0);

        const std::span<const Brick> bricks = grid.get_bricks();
        data.bricks.reserve(bricks.size());
        data.voxels.resize(bricks.size() * brick_voxels);
        data.brick_map.assign(size_t(map_size.x) * map_size.y * map_size.z, empty_brick);

        for (uint32_t i = 0; i < bricks.size(); ++i)
        {
            const Brick& brick = bricks[i];
            data.bricks.push_back({ brick.coord, pack_brick_meta(brick) });
            std::ranges::copy(brick.voxels, data.voxels.begin() + size_t(i) * brick_voxels);
            data.brick_map[brick_map_index(data.header, brick.coord)] = i;
        }
        return data;
    }
}
