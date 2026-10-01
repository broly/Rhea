module;

#include <json/value.h>

export module assets:texture;

import std.compat;

import texture_format;
import dependency_collector;
import rhmath;
import rhobject;
import :asset;

export struct Texture;

export struct TextureHandle : public AssetHandle<TextureHandle>
{
    const Texture& get() const;
    
    bool operator==(const TextureHandle& other) const
    {
        return id == other.id;
    }
    
    std::shared_future<void> resolve_async();
};

export struct TextureHandleHash
{
    size_t operator()(const TextureHandle& m) const
    {
        return m.id;
    }

private:
    template<typename T>
    static void hash_combine(size_t& seed, const T& v)
    {
        seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
};


export struct Texture
{
    std::string name;
    
    Extent extent;
    TextureFormat format;
    uint32_t id;
    
    std::string transition_path;

    std::vector<std::byte> bulk;
    
    static std::optional<Texture> create_from_file(const std::filesystem::path& path);
    bool save_to_file(const std::filesystem::path& path) const;

    // halved in place (2x2 box filter) until both sides are at most max_size (0: no limit); false when it fits
    // already or is not 4 bytes per texel
    bool shrink_to_fit(uint32_t max_size);
};

// cvar render.textures.max_size: material textures larger than this are halved when loaded (0: no limit)
export uint32_t get_texture_max_size();


export void serialize_json_value(TextureHandle& target, const Json::Value& value, const SerializationContext& context);
