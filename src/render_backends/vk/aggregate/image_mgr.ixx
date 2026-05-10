export module vk:image_mgr;

import <vulkan/vulkan_core.h>;
import :instance;
import :immediate_commands;
import :debug;
import render;
import assets;
import texture_format;
import rhmath;
import <optional>;



namespace vk
{
    export class ImageManager
    {
    public:
        ImageManager(vk::Instance& in_instance, vk::ImmediateCommandPool& in_immediate_command_pool, vk::Debug& in_debug,
            vk::VkDebugObjectTracker& in_debug_object_tracker)
            : instance{in_instance}
            , immediate_command_pool(in_immediate_command_pool)
            , debug(in_debug)
            , debug_object_tracker(in_debug_object_tracker)
        {}
        
        RBImageHandle register_swapchain_image(
            VkExtent2D vk_extent, 
            const VkSurfaceFormatKHR& surface_format,
            VkImage image,
            uint32_t image_index,
            std::optional<RBImageHandle> old_image_handle = std::nullopt);
        
        void unregister_swapchain_image(RBImageHandle image_handle);
        
        RBImageView fetch_image_view_generic(RBImageHandle image_handle, 
            uint32_t layer_index = 0, uint32_t mip_level = 0, 
            uint32_t num_mips = 1, uint32_t num_layers = 1, 
            bool is_cubemap = false, bool as_array_2d = false);
        
        vk::ImageResource& get_image_resource(RBImageHandle image_handle);
        const vk::ImageResource& get_image_resource(RBImageHandle image_handle) const;
        VkImageView get_view(RBImageHandle image_handle, uint32_t layer_index = 0, uint32_t mip_index = 0);
        VkImageView get_array_view(RBImageHandle image_handle, uint32_t layer_index, uint32_t num_layers);     // 2D_ARRAY view of all layers
        VkImageView get_cubemap_view(RBImageHandle image_handle);
        
        RBImageHandle create_image(const RBImageDesc& desc);
        
        void destroy_image(RBImageHandle handle, bool wait_fences);


        vk::Instance& instance;
        vk::ImmediateCommandPool& immediate_command_pool;
        vk::Debug& debug;
        vk::VkDebugObjectTracker& debug_object_tracker;
        
        std::vector<vk::ImageResource> image_resources;
        
        void set_default_extent(Extent extent);
        
        bool is_swapchain_image(RBImageHandle image);
        
        RBImageHandle create_texture_2d(const Texture& tex, const TextureCreationInfo& texture_creation_info);
        RBImageHandle create_fallback_texture(const Texture& tex, const TextureCreationInfo& texture_creation_info);
        RBImageHandle create_cubemap(const Cubemap& tex, const TextureCreationInfo& texture_creation_info);
        
        void perform_image_copy(RBCommandList cmd, const CopyImageParams& params);
        
        void transition_image(
            RBCommandList cmd, 
            const ImageBarrierParams& params) const;
        
        VkImageLayout get_image_layout(RBImageHandle image, uint32_t layer = 0, uint32_t mip = 0);
        
        void copy_image_to_buffer(RBImageHandle img, std::vector<float>& buf, TextureFormat& out_format, Extent extent);
        
        void generate_mipmaps(VkCommandBuffer cmd, RBImageHandle image, uint32_t width, uint32_t height, uint32_t mip_levels);
        
        void create_staging_buffer(
            VkDeviceSize size,
            VkBuffer& out_buffer,
            VkDeviceMemory& out_memory) const;
        VkImageSubresourceRange full_subresource_range(RBImageHandle image) const;
        
        ImageReadback readback(RBImageHandle img) const;
        
        
    public:
        struct PendingReadback
        {
            VkBuffer       buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            size_t         total_byte_size = 0;
            Extent         base_extent{};
            uint32_t       layers = 1;
            uint32_t       mips = 1;
            TextureFormat  out_format{};
            uint32_t       channels = 0;
            enum class ComponentType { Float16, Float32, Unorm8 } component_type{};

            struct MipInfo { size_t offset; Extent extent; size_t byte_size; };
            std::vector<std::vector<MipInfo>> mip_infos;
        };

        PendingReadback enqueue_readback(RBCommandList cmd, RBImageHandle img);

        ImageReadback finalize_readback(PendingReadback&& pending) const;
        
        VkFormat get_image_format(RBImageHandle handle) const;

        Extent default_extent;
    };
}