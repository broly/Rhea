export module voxel:io;

import std.compat;

import :grid;

// Voxel grid files (.rvox) and the cache of grids built from meshes.
//
// .rvox, little endian:
//     uint32 magic "RVOX", uint32 version, uint64 content hash (the cache's, 0 otherwise)
//     float voxel_size, float origin[3], uint32 brick count
//     per brick: int32 x, y, z, uint8 generation, uint8 flags, uint16 0, uint32 voxels[512] (local_index order)
// Solid counts are recomputed on load; the dirty flag is not stored.
export namespace voxel
{
    inline constexpr uint32_t file_magic = 0x584f5652;      // "RVOX"
    inline constexpr uint32_t file_version = 1;

    bool write_grid(std::ostream& out, const VoxelGrid& grid, uint64_t content_hash = 0);
    // nullopt with the reason in error (when given) for a damaged file or another version
    std::optional<VoxelGrid> read_grid(std::istream& in, uint64_t* content_hash = nullptr, std::string* error = nullptr);

    // Written next to the target and renamed: a concurrent reader never sees half a file
    bool save_grid(const std::filesystem::path& file, const VoxelGrid& grid, uint64_t content_hash = 0,
        std::string* error = nullptr);
    std::optional<VoxelGrid> load_grid(const std::filesystem::path& file, uint64_t* content_hash = nullptr,
        std::string* error = nullptr);

    // Grids built from source data (voxelized meshes, V2), like the cooked physics shapes in cache/physics:
    // <dir>/<key>.rvox. `hash` covers the source data and the build parameters (voxel:hash): a load with
    // another hash misses, so a changed mesh or voxel size rebuilds. Thread safe.
    class VoxelCache
    {
    public:
        explicit VoxelCache(std::filesystem::path in_dir) : dir(std::move(in_dir)) {}

        std::optional<VoxelGrid> load(std::string_view key, uint64_t hash) const;
        // false with the reason in error when the file could not be written
        bool store(std::string_view key, uint64_t hash, const VoxelGrid& grid, std::string* error = nullptr) const;

        // The file of a key: characters outside [A-Za-z0-9_.-] become '_'
        std::filesystem::path file_of(std::string_view key) const;
        const std::filesystem::path& get_dir() const { return dir; }

    private:
        std::filesystem::path dir;
    };
}
