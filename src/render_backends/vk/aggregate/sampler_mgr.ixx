export module vk:sampler_mgr;

import <vulkan/vulkan_core.h>;

import :instance;
import <unordered_map>;

namespace vk
{
    class SamplerManager
    {
    public:
        SamplerManager(vk::Instance& in_instance)
            : instance{in_instance}
        {}
        
        void init();
    
        VkSampler get_default_sampler() const;
        
        VkSampler default_sampler;
        
        RBSampler get_or_create(const ::SamplerDesc& sampler_info);

        vk::Instance& instance;
        
        std::unordered_map<SamplerDesc, RBSampler, SamplerDescHash> cache;
    };
}
