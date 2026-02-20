export module vk:framebuffer_mgr;

import :instance;
import :image_mgr;
import <unordered_map>;
import <vulkan/vulkan_core.h>;

namespace vk
{
    struct FramebufferResource
    {
        FramebufferDesc desc;
        
        VkFramebuffer framebuffer;
        VkRenderPass  render_pass;
        Extent extent;
        std::vector<VkImageView> attachments;
        
        bool matches(const FramebufferDesc& in_desc, VkRenderPass in_render_pass, Extent in_extent) const
        {
            bool almost_identical = 
                extent == in_extent && 
                in_render_pass == render_pass &&
                desc.attachments.size() == in_desc.attachments.size();
            
            if (!almost_identical)
                return false;
            
            for (uint32_t i = 0; i < desc.attachments.size(); ++i)
                if (desc.attachments[i].image != in_desc.attachments[i].image ||
                desc.attachments[i].layer != in_desc.attachments[i].layer ||
                desc.attachments[i].mip_level != in_desc.attachments[i].mip_level)
                    return false;
            
            return false;
        }
    };
    
    class FramebufferManager
    {
    public:
        FramebufferManager(vk::Instance& in_instance, vk::ImageManager& in_manager)
            : instance{in_instance}
            , image_manager(in_manager)
        {}
        
        vk::Instance& instance;
        vk::ImageManager& image_manager;
        
        void destroy_framebuffers();
        
        RBFramebufferId get_or_create_framebuffer(const FramebufferDesc& desc, VkRenderPass render_pass, Extent extent);
        const FramebufferResource& get_framebuffer_resource(RBFramebufferId framebuffer_id);

        std::unordered_map<size_t, VkFramebuffer> framebuffer_cache;
        std::vector<FramebufferResource> framebuffer_resources;
    };
}
