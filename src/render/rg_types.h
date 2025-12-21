#pragma once
#include <cstdint>

enum class RGResourceType
{
    Invalid,
    Texture,
    Buffer
};

struct RGResourceHandle
{
    std::uint32_t id = UINT32_MAX;

    bool valid() const { return id != UINT32_MAX; }
};

struct RGPassId
{
    uint32_t id;
};

struct RGTextureDesc
{
    uint32_t width  = 0;
    uint32_t height = 0;
};

struct RGBufferDesc
{
    size_t size = 0;
};