export module render:handle_types;

import <cassert>;
import <optional>;
import <vulkan/vulkan_core.h>;
import <GLFW/glfw3.h>;
import <vector>;
import type_id;
import assets;
import name;
import texture_format;
import rhmath;

#include "common/assertion_macros.h"
#include "common/reflect_macros.h"
#include "common/type_macros.h"
import :material_model;
import <map>;
import type_utils;
import enum_helpers;




export enum class RBImageLayout
{
    undefined,
    general,
    color_attachment_optimal,
    depth_stencil_attachment_optimal,
    depth_stencil_read_only_optimal,
    shader_read_only_optimal,
    transfer_src_optimal,
    transfer_dst_optimal,
    preinitialized,
    transfer_present,
};
REFLECT_ENUM(RBImageLayout,
    undefined,
    general,
    color_attachment_optimal,
    depth_stencil_attachment_optimal,
    depth_stencil_read_only_optimal,
    shader_read_only_optimal,
    transfer_src_optimal,
    transfer_dst_optimal,
    preinitialized,
    transfer_present);



export enum class RBImageUsage
{
    Undefined,

    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,

    SampledFragment,
    SampledVertex,

    TransferSrc,
    TransferDst,

    Present
};
REFLECT_ENUM(RBImageUsage,
    Undefined,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    SampledFragment,
    SampledVertex,
    TransferSrc,
    TransferDst,
    Present
    );


export enum class RBLoadOp
{
    Load,
    Clear,
    DontCare
};
REFLECT_ENUM(RBLoadOp,
    Load, Clear, DontCare);
    
    
export enum class RBStoreOp
{
    Store,
    DontCare,
};
REFLECT_ENUM(RBStoreOp,
    Store, DontCare);

export 
{
    template<typename... Ts>
    struct RBHandle
    {
        using AllowedTypes = TypeList<Ts...>;
        
        uintptr_t handle;
        
        AUTO_SPACESHIP(RBHandle, handle);
    #if _DEBUG
        TypeId type_id = nullptr;
    #endif
        
        RBHandle()
        {        
            handle = 0;
        }
        
        RBHandle(const RBHandle& Value)
        {
    #if _DEBUG
            type_id = Value.type_id;
    #endif
            handle = Value.handle;
        };
        
        template<typename T>
        requires AllowedTypes::template contains<T>
        RBHandle(T Value)
        {
    #if _DEBUG
            type_id = get_type_id<T>();
    #endif
            handle = (uintptr_t)(Value);
        }
        
        template<typename T>
        requires AllowedTypes::template contains<T>
        T as() const
        {
    #if _DEBUG
            if (type_id)
            {
                assert(type_id == get_type_id<T>());  // todo: debug compare, remake it to strcmp
            }
    #endif
            return (T)(handle);
        }
        
        friend bool operator==(RBHandle lhs, RBHandle rhs)
        {
    #if _DEBUG
            if (lhs.type_id && rhs.type_id)
            {
                assert(lhs.type_id == rhs.type_id);  // todo: debug compare, remake it to strcmp
            }
    #endif
            return lhs.handle == rhs.handle;
        }

        friend bool operator!=(RBHandle lhs, RBHandle rhs)
        {
            return !(lhs == rhs);
        }
        
        template<typename T>
        requires AllowedTypes::template contains<T>
        RBHandle& operator=(T rhs)
        {
    #if _DEBUG
            if (type_id)
            {
                assert(type_id == get_type_id<T>());  // todo: debug compare, remake it to strcmp
            }
            type_id = get_type_id<T>();
    #endif
            handle = rhs;
            return *this;
        }

        template<typename T>
        requires AllowedTypes::template contains<T>
        operator T() const
        {
            return as<T>();
        }
        
        operator bool() const
        {
            return handle != 0;
        }
    };
    
    template<typename... Ts>
    struct std::hash<RBHandle<Ts...>>
    {
        size_t operator()(const RBHandle<Ts...>& h) const noexcept
        {
            return h.handle;
        }
    };


    // enum class TextureFormat
    // {
    //     Undefined,
    //
    //     RGBA8_UNORM,
    //     RGBA8_SRGB,
    //
    //     RGBA16F,
    //     RGBA32F,
    //
    //     Depth24Stencil8,
    //     Depth32F
    // };


    struct RBBufferHandle
    {
        RBBufferHandle(uint64_t in_handle = 0) 
            : handle(in_handle) {}
        
        RBBufferHandle(uint32_t identifier, ResourceUsage usage_type)
            : identifier(identifier), usage_type(usage_type) {}
        
        AUTO_SPACESHIP(RBBufferHandle, handle);
        
        ResourceUsage get_usage_type() const
        {
            return usage_type;
        }
        
        uint32_t get_identifier() const
        {
            return identifier;
        }
        
    protected:
        union
        {
            struct
            {
                uint32_t identifier;
                ResourceUsage usage_type;
            };
            uint64_t handle = 0;
        };
    };


    // just universal integer handle types
    using RBCommandList = RBHandle<VkCommandBuffer>;
    using RBPipelineHandle = RBHandle<VkPipeline>;
    using RBDescriptorSet = RBHandle<VkDescriptorSet>;
    using RBDescriptorSetLayout = RBHandle<uint64_t>; // temporary
    
    using RBObject = RBHandle<uint64_t>;

    using RBWindowHandle = RBHandle<GLFWwindow*>;

    using RBFramebufferId = RBHandle<uint64_t>;
    using RBFrameHandle = uint32_t;
    
    using RBVertexBufferHandle = RBHandle<uint32_t>;
    
    using RBPipelineLayout = RBHandle<VkPipelineLayout>;


    enum class TextureDimension
    {
        Tex2D,
        Cube
    };


    namespace RenderTextureUsage
    {
        enum Type : uint32_t
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
    }

    struct RBImageDesc
    {
        Name name;
        Extent extent;
        TextureFormat format;
        Mask<RenderTextureUsage::Type> usage;
        uint32_t mip_levels = 1;
        uint32_t array_layers = 1;
        bool is_cubemap = false;
        
        uint32_t get_array_layers() const
        {
            if (is_cubemap)
                checkf(array_layers == 6, "cubemap texture array_layers must be 6");
            return array_layers;
        }
    };

    struct RBImageHandle
    {
        uint32_t id = 0;

        bool operator==(const RBImageHandle&) const = default;
    };

    using RBImageView = RBHandle<VkImageView>;

    using RBSampler = RBHandle<VkSampler>;
    
    
    struct AttachmentDesc
    {
        Name image_name;
        RBImageHandle image;
        RBLoadOp load;
        RBStoreOp store;
        RBImageUsage usage;
        uint32_t layer = 0;
        uint32_t mip_level = 0;
        bool depth_attachment = false;
    };
    


    struct FramebufferDesc
    {
        Name pass;
        std::vector<AttachmentDesc> attachments;
        //std::optional<AttachmentDesc> depth_attachment;
        
        void update_extent(const Extent& new_extent)
        {
            extent = new_extent;
        }
        
        bool is_extent_set() const
        {
            return extent.is_not_zero();
        }
        

        Extent extent;
    };
    
    struct RBDrawParams
    {
        bool update_viewport_extent = false;
        bool use_swapchain_extent = false;
        Extent extent = {};
    };

    using RBRenderPass = RBHandle<VkRenderPass>;
    using RBSampler = RBHandle<VkSampler>;

    struct RBSamplerDesc
    {
        bool linear = true;
        bool clamp_to_edge = true;
    };
    
    
    struct ImageBarrierParams
    {
        RBImageHandle image = {};
        RBImageLayout before = RBImageLayout::undefined;
        RBImageLayout after = RBImageLayout::undefined;
        uint32_t base_layer = 0;
        uint32_t base_mip = 0;
        uint32_t layer_count = 0;
        uint32_t mip_count = 0;
        bool log = false;
    };
}
