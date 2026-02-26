export module render:rg_types;

import <cstdint>;

import :handle_types;
import enum_helpers;

#include "common/type_macros.h"

export 
{
    enum class RTBuildMode
    {
        none,
        build_blas,
    };
    
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
        Extent extent;

        TextureFormat format = TextureFormat::Undefined;
        Mask<RenderTextureUsage::Type> usage = RenderTextureUsage::None;
        
        

        bool external  = false;  // swapchain / imported resource
        bool imported  = false;
        
        bool imported_texture_layout_initialized = false;
        
        bool swapchain_image = false;
        
        TextureDimension dimension = TextureDimension::Tex2D;
        uint32_t mip_levels = 1;
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


    struct RGTextureHandle
    {
        uint32_t id;
        Name name;
        
        auto operator<=>(const RGTextureHandle& rhs) const
        {
            return id <=> rhs.id;
        };
    };
    
    template<>
    struct std::hash<RGTextureHandle>
    {
        size_t operator()(const RGTextureHandle& h) const noexcept
        {
            return h.id;
        }
    };
    
    struct BarrierInfo
    {
        RBImageLayout layout;
    };

    struct RGImageBarriers
    {
        std::optional<BarrierInfo> before_pass;  // not needed for ColorAttachment / DepthAttachment
        std::optional<BarrierInfo> after_pass;  // will be read as: attachment -> (Sampled, Present, Storage)
    };

    struct RGImageUse
    {
        RGTextureHandle texture;
        RBImageUsage    usage;
        RBLoadOp load_op = RBLoadOp::Load;
        RBStoreOp store_op = RBStoreOp::Store;
    };
    
    using RBDeviceAddress = RBHandle<VkDeviceAddress>;

    using RBFormat = RBHandle<VkFormat>;
    
}
