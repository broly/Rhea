export module assets:cubemap;
import texture_format;
import :asset;
import <array>;
import <vector>;
import <string>;
import <future>;
import <filesystem>;
import <json/value.h>;
import texture_format;
import dependency_collector;
import name;

export struct Cubemap
{
    Name name;
    uint32_t id;
    TextureFormat format = TextureFormat::RGBA32F;
    std::array<std::vector<std::vector<float>>, 6> faces;
    uint32_t face_size = 0;
    
    void save(const std::filesystem::path& filename) const;
    
    static std::optional<Cubemap> load(const std::filesystem::path& filename);
};


export struct CubemapHandle : AssetHandle<CubemapHandle>
{
    const Cubemap& get() const;
    
    bool operator==(const CubemapHandle& other) const
    {
        return id == other.id;
    }
    
    std::shared_future<void> resolve_async();
};

export struct CubemapHandleHash
{
    size_t operator()(const CubemapHandle& m) const
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

export void serialize_json_value(CubemapHandle& target, const Json::Value& value, DependencyCollector* dc);
