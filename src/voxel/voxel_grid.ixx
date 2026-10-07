export module voxel:grid;

import std.compat;
import glm;

// Sparse voxel grid: 8x8x8 bricks of RGBA8 voxels (sRGB albedo + density), only where there is something.
//
// The brick is the unit of everything built on it: the 3D JPEG compresses one brick as one 8x8x8 DCT block
// (V4), integrity counts solid voxels per brick on the CPU without GPU readback (V6), a detached piece of a
// house is a set of bricks (V6). Voxel coordinates are integers in grid space; the grid sits in the local space
// of its object:
//
//     local position of voxel v = origin + (v + 0.5) * voxel_size       (its center)
//     brick of voxel v = floor(v / 8), its voxel inside the brick = v - brick * 8
//
// Not thread safe; one writer at a time.
export namespace voxel
{
    inline constexpr int32_t brick_size = 8;
    inline constexpr int32_t brick_shift = 3;                      // log2(brick_size)
    inline constexpr uint32_t brick_voxels = 512;                  // brick_size^3

    // Density at and above which a voxel is solid (drawn, counted for integrity, collides)
    inline constexpr uint8_t solid_density = 128;

    // One voxel, packed as on the GPU: r | g << 8 | b << 16 | density << 24 (GLSL unpackUnorm4x8)
    struct Voxel
    {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t density = 0;                                       // 0: empty, 255: full

        constexpr bool is_solid() const { return density >= solid_density; }
        constexpr bool is_empty() const { return density == 0; }

        constexpr uint32_t pack() const
        {
            return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(density) << 24;
        }
        static constexpr Voxel unpack(uint32_t bits)
        {
            return { uint8_t(bits), uint8_t(bits >> 8), uint8_t(bits >> 16), uint8_t(bits >> 24) };
        }

        friend constexpr bool operator==(const Voxel&, const Voxel&) = default;
    };

    constexpr bool is_solid_bits(uint32_t bits) { return (bits >> 24) >= solid_density; }

    // Voxel inside its brick: x + y * 8 + z * 64 (the order of the GPU pool and of the files)
    constexpr uint32_t local_index(glm::ivec3 local)
    {
        return uint32_t(local.x) | uint32_t(local.y) << brick_shift | uint32_t(local.z) << (2 * brick_shift);
    }
    constexpr glm::ivec3 local_coord(uint32_t index)
    {
        return { int32_t(index & 7u), int32_t((index >> brick_shift) & 7u), int32_t(index >> (2 * brick_shift)) };
    }

    // floor division by 8, also for negative coordinates
    constexpr glm::ivec3 brick_of(glm::ivec3 voxel)
    {
        return { voxel.x >> brick_shift, voxel.y >> brick_shift, voxel.z >> brick_shift };
    }
    constexpr glm::ivec3 local_of(glm::ivec3 voxel)
    {
        return { voxel.x & (brick_size - 1), voxel.y & (brick_size - 1), voxel.z & (brick_size - 1) };
    }

    namespace brick_flags
    {
        inline constexpr uint8_t none = 0;
        inline constexpr uint8_t detached = 1 << 0;     // broke off the object (V6): simulated as a body
        inline constexpr uint8_t dirty = 1 << 1;        // changed on the CPU since the last GPU upload
    }

    struct Brick
    {
        glm::ivec3 coord{ 0 };                          // in bricks
        uint16_t solid_count = 0;                       // voxels with density >= solid_density
        uint8_t generation = 0;                         // times the 3D JPEG recompressed it (V4)
        uint8_t flags = brick_flags::none;
        std::array<uint32_t, brick_voxels> voxels{};    // packed Voxel, local_index order

        Voxel get(glm::ivec3 local) const { return Voxel::unpack(voxels[local_index(local)]); }
        // Keeps solid_count
        void set(glm::ivec3 local, Voxel value);
        // After writing voxels directly
        void recount();
        bool is_empty() const;                          // every voxel has density 0
    };

    struct GridBounds
    {
        glm::ivec3 min{ 0 };                            // inclusive
        glm::ivec3 max{ -1 };                           // inclusive; min > max: empty

        bool is_empty() const { return min.x > max.x; }
        glm::ivec3 size() const { return is_empty() ? glm::ivec3(0) : max - min + 1; }
    };

    class VoxelGrid
    {
    public:
        explicit VoxelGrid(float voxel_size = 0.1f, const glm::vec3& origin = glm::vec3(0.0f));

        float get_voxel_size() const { return voxel_size; }
        const glm::vec3& get_origin() const { return origin; }

        // --- coordinates ---

        // Voxel containing a local position
        glm::ivec3 voxel_at(const glm::vec3& local_position) const;
        glm::vec3 voxel_center(glm::ivec3 voxel) const;
        glm::vec3 brick_min_corner(glm::ivec3 brick) const;
        float get_brick_extent() const { return voxel_size * float(brick_size); }

        // --- voxels ---

        // Empty outside the bricks
        Voxel get(glm::ivec3 voxel) const;
        bool is_solid(glm::ivec3 voxel) const { return get(voxel).is_solid(); }
        // Adds the brick for a non-empty value; an emptied brick stays until remove_empty_bricks
        void set(glm::ivec3 voxel, Voxel value);

        // --- bricks ---
        // Indices are stable until a brick is removed (removal moves the last brick into the hole)

        uint32_t get_brick_count() const { return uint32_t(bricks.size()); }
        Brick& get_brick(uint32_t index) { return bricks[index]; }
        const Brick& get_brick(uint32_t index) const { return bricks[index]; }
        std::span<Brick> get_bricks() { return bricks; }
        std::span<const Brick> get_bricks() const { return bricks; }

        std::optional<uint32_t> find_brick(glm::ivec3 brick) const;
        // The brick at a brick coordinate, added (empty) if missing
        uint32_t add_brick(glm::ivec3 brick);
        void remove_brick(glm::ivec3 brick);
        // Returns how many were removed
        uint32_t remove_empty_bricks();
        void clear();

        // --- totals ---

        // In bricks; empty when there are none
        GridBounds get_brick_bounds() const;
        // Local space box of the bricks (min corner, max corner)
        std::pair<glm::vec3, glm::vec3> get_local_bounds() const;
        uint64_t get_solid_count() const;
        // Solid voxels with at least one of their 6 neighbours not solid: what is drawn
        uint64_t count_surface_voxels() const;

        // Content hash: voxel size, origin, every brick (order independent)
        uint64_t compute_hash() const;

    private:
        static uint64_t brick_key(glm::ivec3 brick);

        float voxel_size;
        glm::vec3 origin;
        std::vector<Brick> bricks;
        std::unordered_map<uint64_t, uint32_t> brick_lookup;      // brick_key -> index
    };
}
