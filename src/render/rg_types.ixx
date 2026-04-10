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
        uint32_t num_mip_levels = 1;
        uint32_t num_layers = 1;
        uint8_t num_frames = 1;
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
        RBImageUsageType src_usage = RBImageUsageType::Undefined;
        RBImageUsageType dst_usage;
        bool is_transition;
    };

    struct RGImageBarriers
    {
        std::optional<BarrierInfo> before_pass;  // not needed for ColorAttachment / DepthAttachment
        std::optional<BarrierInfo> after_pass;  // will be read as: attachment -> (Sampled, Present, Storage)
    };

    struct RBImageUsage
    {
        RGTextureHandle texture;
        RBImageUsageType usage_type;
        RBLoadOp load_op = RBLoadOp::Load;
        RBStoreOp store_op = RBStoreOp::Store;
    };
    
    using RBDeviceAddress = RBHandle<VkDeviceAddress>;

    using RBFormat = RBHandle<VkFormat>;
    
}
