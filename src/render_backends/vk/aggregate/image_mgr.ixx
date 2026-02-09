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
        ImageManager(vk::Instance& in_instance, vk::ImmediateCommandPool& in_immediate_command_pool, vk::Debug& in_debug)
            : instance{in_instance}
            , immediate_command_pool(in_immediate_command_pool)
            , debug(in_debug)
        {}
        
        RBImageHandle create_image_view(
            VkExtent2D vk_extent, 
            const VkSurfaceFormatKHR& surface_format,
            VkImage image,
            uint32_t array_index = 0);
        
        RBImageView fetch_image_view_generic(RBImageHandle image_handle, uint32_t layer_index = 0, uint32_t mip_level = 0, bool is_cubemap = false);
        
        vk::ImageResource& get_image_resource(RBImageHandle image_handle);
        const vk::ImageResource& get_image_resource(RBImageHandle image_handle) const;
        VkImageView get_view(RBImageHandle image_handle, uint32_t array_index = 0, uint32_t mip_index = 0);
        VkImageView get_cubemap_view(RBImageHandle image_handle);
        
        RBImageHandle create_image(const RBImageDesc& desc);
        
        void destroy_image(RBImageHandle handle, bool wait_fences);


        vk::Instance& instance;
        vk::ImmediateCommandPool& immediate_command_pool;
        vk::Debug& debug;
        
        std::vector<vk::ImageResource> image_resources;
        
        void set_default_extent(Extent extent);
        
        RBImageHandle create_texture_2d(const Texture& tex, const TextureCreationInfo& texture_creation_info);
        RBImageHandle create_cubemap(const Cubemap& tex, const TextureCreationInfo& texture_creation_info);
        
        void transition_image(
            RBCommandList cmd,
            RBImageHandle image,
            RBImageLayout before,
            RBImageLayout after) const;
        
        void copy_image_to_buffer(RBImageHandle img, std::vector<float>& buf, TextureFormat& out_format, Extent extent);
        
        void generate_mipmaps(VkCommandBuffer cmd, RBImageHandle image, uint32_t width, uint32_t height, uint32_t mip_levels);
        
        void create_staging_buffer(
            VkDeviceSize size,
            VkBuffer& out_buffer,
            VkDeviceMemory& out_memory) const;
        VkImageSubresourceRange full_subresource_range(RBImageHandle image) const;
        
        ImageReadback readback(RBImageHandle img) const;
        
        
        VkFormat get_image_format(RBImageHandle handle) const;

        Extent default_extent;
    };
}