export module render:rg_types;

import <cstdint>;

import :handle_types;
import enum_helpers;

#include "common/type_macros.h"

export 
{

    
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


    struct RGTextureDesc
    {
        uint32_t width  = 0;
        uint32_t height = 0;    

        TextureFormat format = TextureFormat::Undefined;
        Mask<RenderTextureUsage::Type> usage = RenderTextureUsage::None;

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

    struct RGTexture
    {
        RGTextureDesc desc;
        std::optional<RBImageHandle> image;      // backend image
        
        RBImageUsage current_usage = RBImageUsage::Undefined;
    };

    struct RGTextureHandle
    {
        uint32_t id;
    };

    struct RGImageBarrier
    {
        RGTextureHandle texture;
        RBImageUsage    before;
        RBImageUsage    after;
    };


    struct RGImageUse
    {
        RGTextureHandle texture;
        RBImageUsage    usage;
    };

    using RBFormat = RBHandle<VkFormat>;




    struct RenderPassDesc
    {
        std::vector<RBFormat> color_formats;
        RBFormat depth_format;
        
        bool has_depth = false;

        bool operator==(const RenderPassDesc&) const = default;
    };

    struct RenderPassDescHash
    {
        size_t operator()(const RenderPassDesc& d) const
        {
            size_t h = d.color_formats.size();
            for (auto f : d.color_formats)
                h ^= std::hash<uint32_t>()(f + 0x9e3779b9 + (h<<6) + (h>>2));

            if (d.has_depth)
                h ^= std::hash<uint32_t>()(d.depth_format);

            return h;
        }
    };
}
