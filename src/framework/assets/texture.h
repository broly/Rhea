#pragma once
#include <cstdint>
#include <vector>

struct TextureHandle
{
    uint32_t id;
};

enum class TextureFormat
{
    RGB8,
    RGBA8
};

struct Texture
{
    uint32_t width;
    uint32_t height;
    TextureFormat format;

    std::vector<uint8_t> pixels;
};
