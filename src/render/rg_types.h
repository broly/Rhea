#pragma once
#include <cstdint>

#include "common/type_macros.h"


enum class RGResourceType {
    SwapchainColor,

    Depth,

    GBuffer_Albedo,
    GBuffer_Normal,
    GBuffer_MetallicRoughness,
    GBuffer_Emissive,

    ShadowMap,
    SSAO,
    Lighting,
};

enum class RGResourceKind
{
    Texture,
    Buffer,
};

enum class RGTextureFormat
{
    Undefined,

    RGBA8_UNORM,
    RGBA8_SRGB,

    RGBA16F,
    RGBA32F,

    Depth24Stencil8,
    Depth32F
};

enum class RGTextureUsage : uint32_t
{
    None            = 0,
    ColorAttachment = 1 << 0,
    DepthStencil    = 1 << 1,
    Sampled         = 1 << 2,
    Storage         = 1 << 3,
    TransferSrc     = 1 << 4,
    TransferDst     = 1 << 5,
    Present         = 1 << 6
};
ENUM_MASK_OPS(RGTextureUsage);


struct RGTextureDesc
{
    uint32_t width  = 0;
    uint32_t height = 0;

    RGTextureFormat format = RGTextureFormat::Undefined;
    RGTextureUsage  usage  = RGTextureUsage::None;

    bool transient = true;   // may be allocated per-frame
    bool external  = false;  // swapchain / imported resource
};


struct RGBufferDesc
{
    size_t size = 0;
};

struct RGResource
{
    RGResourceKind kind;
    RGTextureDesc texture_desc{};
    RGBufferDesc  buffer_desc{};
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
