module vk:texture_mgr;

import <vulkan/vulkan_core.h>;

VkSampler vk::TextureManager::get_default_sampler() const
{
    
    static bool created_default_sampler = false;
    
    static VkSampler default_sampler;
    
    if (!created_default_sampler)
    {
        created_default_sampler = true;
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        info.compareEnable = VK_FALSE; // IMPORTANT
        info.compareOp = VK_COMPARE_OP_ALWAYS;

        info.minLod = 0.0f;
        info.maxLod = 1.0f;
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        vkCreateSampler(instance.get_device(), &info, nullptr, &default_sampler);
    }
    
    return default_sampler;
}
