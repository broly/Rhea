#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
#include <json/value.h>

#include "asset.h"

struct Texture;

struct TextureHandle : public AssetHandle<TextureHandle>
{
    const Texture& get() const;
    
    bool operator==(const TextureHandle& other) const
    {
        return id == other.id;
    }
};

struct TextureHandleHash
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


enum class TextureFormat
{
    RGB8,
    RGBA8,
    RGBA8_SRGB
};

struct Texture
{
    std::string name;
    
    uint32_t width;
    uint32_t height;
    TextureFormat format;

    std::vector<uint8_t> pixels;
    
    static Texture create_from_file(const std::filesystem::path& path);
    
};


void serialize_json_value(TextureHandle& target, const Json::Value& value);
