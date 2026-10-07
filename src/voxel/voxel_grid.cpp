module voxel;

import std.compat;
import glm;

import :grid;
import :hash;

namespace
{
    // Brick coordinates in a key: 21 bits each, +-1M bricks = +-800 km at 10 cm voxels
    constexpr int32_t key_bias = 1 << 20;
    constexpr uint64_t key_mask = (1ull << 21) - 1;
}

namespace voxel
{
    // ------------------------------------------------------------------------------------------------
    // Brick
    // ------------------------------------------------------------------------------------------------

    void Brick::set(glm::ivec3 local, Voxel value)
    {
        uint32_t& bits = voxels[local_index(local)];
        solid_count = uint16_t(solid_count - (is_solid_bits(bits) ? 1 : 0) + (value.is_solid() ? 1 : 0));
        bits = value.pack();
    }

    void Brick::recount()
    {
        uint32_t count = 0;
        for (uint32_t bits : voxels)
            count += is_solid_bits(bits) ? 1u : 0u;
        solid_count = uint16_t(count);
    }

    bool Brick::is_empty() const
    {
        return std::ranges::all_of(voxels, [] (uint32_t bits) { return (bits >> 24) == 0; });
    }

    // ------------------------------------------------------------------------------------------------
    // VoxelGrid
    // ------------------------------------------------------------------------------------------------

    VoxelGrid::VoxelGrid(float in_voxel_size, const glm::vec3& in_origin)
        : voxel_size(in_voxel_size)
        , origin(in_origin)
    {
    }

    uint64_t VoxelGrid::brick_key(glm::ivec3 brick)
    {
        return (uint64_t(brick.x + key_bias) & key_mask)
            | (uint64_t(brick.y + key_bias) & key_mask) << 21
            | (uint64_t(brick.z + key_bias) & key_mask) << 42;
    }

    glm::ivec3 VoxelGrid::voxel_at(const glm::vec3& local_position) const
    {
        return glm::ivec3(glm::floor((local_position - origin) / voxel_size));
    }

    glm::vec3 VoxelGrid::voxel_center(glm::ivec3 voxel) const
    {
        return origin + (glm::vec3(voxel) + 0.5f) * voxel_size;
    }

    glm::vec3 VoxelGrid::brick_min_corner(glm::ivec3 brick) const
    {
        return origin + glm::vec3(brick * brick_size) * voxel_size;
    }

    Voxel VoxelGrid::get(glm::ivec3 voxel) const
    {
        const std::optional<uint32_t> index = find_brick(brick_of(voxel));
        return index ? bricks[*index].get(local_of(voxel)) : Voxel{};
    }

    void VoxelGrid::set(glm::ivec3 voxel, Voxel value)
    {
        const glm::ivec3 brick = brick_of(voxel);
        std::optional<uint32_t> index = find_brick(brick);
        if (!index)
        {
            if (value.is_empty())
                return;
            index = add_brick(brick);
        }
        bricks[*index].set(local_of(voxel), value);
    }

    std::optional<uint32_t> VoxelGrid::find_brick(glm::ivec3 brick) const
    {
        if (const auto it = brick_lookup.find(brick_key(brick)); it != brick_lookup.end())
            return it->second;
        return std::nullopt;
    }

    uint32_t VoxelGrid::add_brick(glm::ivec3 brick)
    {
        const auto [it, added] = brick_lookup.try_emplace(brick_key(brick), uint32_t(bricks.size()));
        if (added)
        {
            Brick& new_brick = bricks.emplace_back();
            new_brick.coord = brick;
        }
        return it->second;
    }

    void VoxelGrid::remove_brick(glm::ivec3 brick)
    {
        const auto it = brick_lookup.find(brick_key(brick));
        if (it == brick_lookup.end())
            return;
        const uint32_t index = it->second;
        brick_lookup.erase(it);
        if (index != bricks.size() - 1)
        {
            bricks[index] = std::move(bricks.back());
            brick_lookup[brick_key(bricks[index].coord)] = index;
        }
        bricks.pop_back();
    }

    uint32_t VoxelGrid::remove_empty_bricks()
    {
        std::vector<glm::ivec3> empty;
        for (const Brick& brick : bricks)
            if (brick.is_empty())
                empty.push_back(brick.coord);
        for (const glm::ivec3& coord : empty)
            remove_brick(coord);
        return uint32_t(empty.size());
    }

    void VoxelGrid::clear()
    {
        bricks.clear();
        brick_lookup.clear();
    }

    GridBounds VoxelGrid::get_brick_bounds() const
    {
        GridBounds bounds;
        if (bricks.empty())
            return bounds;
        bounds.min = bounds.max = bricks.front().coord;
        for (const Brick& brick : bricks)
        {
            bounds.min = glm::min(bounds.min, brick.coord);
            bounds.max = glm::max(bounds.max, brick.coord);
        }
        return bounds;
    }

    std::pair<glm::vec3, glm::vec3> VoxelGrid::get_local_bounds() const
    {
        const GridBounds bounds = get_brick_bounds();
        if (bounds.is_empty())
            return { origin, origin };
        return { brick_min_corner(bounds.min), brick_min_corner(bounds.max + 1) };
    }

    uint64_t VoxelGrid::get_solid_count() const
    {
        uint64_t count = 0;
        for (const Brick& brick : bricks)
            count += brick.solid_count;
        return count;
    }

    uint64_t VoxelGrid::count_surface_voxels() const
    {
        static constexpr glm::ivec3 neighbours[6] = {
            { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
        };

        uint64_t count = 0;
        for (const Brick& brick : bricks)
        {
            if (brick.solid_count == 0)
                continue;
            const glm::ivec3 base = brick.coord * brick_size;
            for (uint32_t i = 0; i < brick_voxels; ++i)
            {
                if (!is_solid_bits(brick.voxels[i]))
                    continue;
                const glm::ivec3 local = local_coord(i);
                for (const glm::ivec3& offset : neighbours)
                {
                    const glm::ivec3 n = local + offset;
                    const bool inside = n.x >= 0 && n.y >= 0 && n.z >= 0
                        && n.x < brick_size && n.y < brick_size && n.z < brick_size;
                    const bool solid = inside ? is_solid_bits(brick.voxels[local_index(n)]) : is_solid(base + n);
                    if (!solid)
                    {
                        ++count;
                        break;
                    }
                }
            }
        }
        return count;
    }

    uint64_t VoxelGrid::compute_hash() const
    {
        // bricks in coordinate order: the same content hashes the same whatever the insertion order
        std::vector<uint32_t> order(bricks.size());
        std::iota(order.begin(), order.end(), 0u);
        std::ranges::sort(order, [this] (uint32_t a, uint32_t b) {
            return brick_key(bricks[a].coord) < brick_key(bricks[b].coord);
        });

        uint64_t h = hash_seed;
        h = hash_value(h, voxel_size);
        h = hash_value(h, origin);
        for (uint32_t index : order)
        {
            const Brick& brick = bricks[index];
            h = hash_value(h, brick.coord);
            h = hash_value(h, brick.generation);
            h = hash_bytes(h, brick.voxels.data(), sizeof(brick.voxels));
        }
        return h;
    }
}
