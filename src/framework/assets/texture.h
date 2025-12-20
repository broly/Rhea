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
};

enum class TextureFormat
{
    RGB8,
    RGBA8
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
