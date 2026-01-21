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
        Name name;
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
        Name name;
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
        RBLoadOp load_op = RBLoadOp::Load;
    };

    using RBFormat = RBHandle<VkFormat>;
    
}
