export module voxel:hash;

import std.compat;

// Content hashes for the voxel cache (FNV-1a 64): chain the source data and the build parameters, e.g.
//
//     uint64_t h = voxel::hash_bytes(voxel::hash_seed, vertices.data(), vertices.size() * sizeof(glm::vec3));
//     h = voxel::hash_value(h, voxel_size);
export namespace voxel
{
    inline constexpr uint64_t hash_seed = 0xcbf29ce484222325ull;

    inline uint64_t hash_bytes(uint64_t h, const void* data, size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
            h = (h ^ bytes[i]) * 0x100000001b3ull;
        return h;
    }

    // Trivially copyable values without padding (numbers, glm vectors): padding bytes would make the hash random
    template<typename T>
    uint64_t hash_value(uint64_t h, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return hash_bytes(h, &value, sizeof(T));
    }

    inline uint64_t hash_string(uint64_t h, std::string_view text)
    {
        return hash_bytes(h, text.data(), text.size());
    }
}
