export module vk:texture_mgr;

import <vulkan/vulkan_core.h>;
import :instance;

namespace vk
{
    class TextureManager
    {
    public:
        TextureManager(vk::Instance& in_instance)
            : instance{in_instance}
        {}
        
        VkSampler get_default_sampler() const;
        
        vk::Instance& instance;
    };
}