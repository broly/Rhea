export module assets:texture;

import <cstdint>;
import <filesystem>;
import <vector>;
import <future>;
import <json/value.h>;
import texture_format;
import dependency_collector;


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
    
    uint32_t width;
    uint32_t height;
    TextureFormat format;
    uint32_t id;

    std::vector<uint8_t> pixels;
    
    static std::optional<Texture> create_from_file(const std::filesystem::path& path);
    
};


export void serialize_json_value(TextureHandle& target, const Json::Value& value, DependencyCollector* dc);
