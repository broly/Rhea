module voxel;

import std.compat;
import glm;

import :grid;
import :io;

namespace
{
    using namespace voxel;

    // a damaged count must not allocate the world: 4M bricks = 8 GiB of voxels
    constexpr uint32_t max_bricks = 1u << 22;

    struct BrickRecord
    {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        uint8_t generation = 0;
        uint8_t flags = 0;
        uint16_t reserved = 0;
    };
    static_assert(sizeof(BrickRecord) == 16);

    // flags that describe the content; dirty is about this session's GPU copy
    constexpr uint8_t stored_flags = brick_flags::detached;

    template<typename T>
    void write_pod(std::ostream& out, const T& value)
    {
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template<typename T>
    bool read_pod(std::istream& in, T& value)
    {
        return bool(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
    }

    void set_error(std::string* error, std::string text)
    {
        if (error)
            *error = std::move(text);
    }
}

namespace voxel
{
    bool write_grid(std::ostream& out, const VoxelGrid& grid, uint64_t content_hash)
    {
        write_pod(out, file_magic);
        write_pod(out, file_version);
        write_pod(out, content_hash);
        write_pod(out, grid.get_voxel_size());
        write_pod(out, grid.get_origin());
        write_pod(out, grid.get_brick_count());
        for (const Brick& brick : grid.get_bricks())
        {
            const BrickRecord record{ brick.coord.x, brick.coord.y, brick.coord.z, brick.generation,
                uint8_t(brick.flags & stored_flags), 0 };
            write_pod(out, record);
            out.write(reinterpret_cast<const char*>(brick.voxels.data()), sizeof(brick.voxels));
        }
        return bool(out);
    }

    std::optional<VoxelGrid> read_grid(std::istream& in, uint64_t* content_hash, std::string* error)
    {
        uint32_t magic = 0;
        uint32_t version = 0;
        uint64_t hash = 0;
        float voxel_size = 0.0f;
        glm::vec3 origin{ 0.0f };
        uint32_t brick_count = 0;
        if (!read_pod(in, magic) || !read_pod(in, version) || !read_pod(in, hash) || !read_pod(in, voxel_size)
            || !read_pod(in, origin) || !read_pod(in, brick_count))
        {
            set_error(error, "truncated header");
            return std::nullopt;
        }
        if (magic != file_magic)
        {
            set_error(error, "not a voxel grid file");
            return std::nullopt;
        }
        if (version != file_version)
        {
            set_error(error, std::format("version {}, expected {}", version, file_version));
            return std::nullopt;
        }
        if (!(voxel_size > 0.0f) || brick_count > max_bricks)
        {
            set_error(error, std::format("bad header: voxel size {}, {} bricks", voxel_size, brick_count));
            return std::nullopt;
        }

        VoxelGrid grid(voxel_size, origin);
        for (uint32_t i = 0; i < brick_count; ++i)
        {
            BrickRecord record;
            if (!read_pod(in, record))
            {
                set_error(error, std::format("truncated at brick {} of {}", i, brick_count));
                return std::nullopt;
            }
            Brick& brick = grid.get_brick(grid.add_brick({ record.x, record.y, record.z }));
            brick.generation = record.generation;
            brick.flags = uint8_t(record.flags & stored_flags);
            if (!in.read(reinterpret_cast<char*>(brick.voxels.data()), sizeof(brick.voxels)))
            {
                set_error(error, std::format("truncated at brick {} of {}", i, brick_count));
                return std::nullopt;
            }
            brick.recount();
        }

        if (content_hash)
            *content_hash = hash;
        return grid;
    }

    bool save_grid(const std::filesystem::path& file, const VoxelGrid& grid, uint64_t content_hash, std::string* error)
    {
        std::error_code ec;
        if (file.has_parent_path())
            std::filesystem::create_directories(file.parent_path(), ec);

        std::filesystem::path temp_file = file;
        temp_file += std::format(".{}.tmp", std::hash<std::thread::id>{}(std::this_thread::get_id()));
        bool written = false;
        {
            std::ofstream out(temp_file, std::ios::binary | std::ios::trunc);
            written = out && write_grid(out, grid, content_hash);
        }
        if (written)
            std::filesystem::rename(temp_file, file, ec);
        if (!written || ec)
        {
            set_error(error, written ? ec.message() : std::string("write failed"));
            std::filesystem::remove(temp_file, ec);
            return false;
        }
        return true;
    }

    std::optional<VoxelGrid> load_grid(const std::filesystem::path& file, uint64_t* content_hash, std::string* error)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
        {
            set_error(error, "can't open");
            return std::nullopt;
        }
        return read_grid(in, content_hash, error);
    }

    // ------------------------------------------------------------------------------------------------
    // VoxelCache
    // ------------------------------------------------------------------------------------------------

    std::filesystem::path VoxelCache::file_of(std::string_view key) const
    {
        std::string name(key);
        for (char& c : name)
            if (!std::isalnum(uint8_t(c)) && c != '_' && c != '-' && c != '.')
                c = '_';
        return dir / (name + ".rvox");
    }

    std::optional<VoxelGrid> VoxelCache::load(std::string_view key, uint64_t hash) const
    {
        if (key.empty() || dir.empty())
            return std::nullopt;
        uint64_t stored_hash = 0;
        std::optional<VoxelGrid> grid = load_grid(file_of(key), &stored_hash);
        if (!grid || stored_hash != hash)
            return std::nullopt;
        return grid;
    }

    bool VoxelCache::store(std::string_view key, uint64_t hash, const VoxelGrid& grid, std::string* error) const
    {
        if (key.empty() || dir.empty())
        {
            set_error(error, "no cache key or directory");
            return false;
        }
        return save_grid(file_of(key), grid, hash, error);
    }
}
