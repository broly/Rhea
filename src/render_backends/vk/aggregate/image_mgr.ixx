export module vk:image_mgr;

import <vulkan/vulkan_core.h>;
import :instance;



namespace vk
{
    export class ImageManager
    {
    public:
        ImageManager(vk::Instance& in_instance)
            : instance{in_instance}
        {}
        
        VkSampler get_default_sampler() const;
        
        RBImageHandle create_image_view(
            VkExtent2D extent, 
            const VkSurfaceFormatKHR& surface_format,
            VkImage image);
        
        vk::ImageResource& get_image_resource(RBImageHandle image_handle);
        VkImageView get_image_view(RBImageHandle image_handle);
        
        RBImageHandle create_image(const RBImageDesc& desc);
        
        vk::Instance& instance;
        
        std::vector<vk::ImageResource> image_resources;
        
        void set_default_extent(uint32_t width, uint32_t height);
        
        
        VkFormat get_image_format(RBImageHandle handle) const;

        uint32_t default_width;
        uint32_t default_height;
    };
}