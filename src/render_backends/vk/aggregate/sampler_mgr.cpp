module vk:sampler_mgr;

import <vulkan/vulkan_core.h>;
import :log;
import :sampler_desc;
import <string>;

void vk::SamplerManager::init()
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.mipLodBias = 0.0f;

    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.anisotropyEnable = VK_FALSE;
    info.maxAnisotropy = 16.f;

    info.compareEnable = VK_FALSE; // IMPORTANT
    info.compareOp = VK_COMPARE_OP_ALWAYS;

    info.minLod = 0.0f;
    info.maxLod = 0.0f;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    vkCreateSampler(instance.get_device(), &info, nullptr, &default_sampler);
}

VkSampler vk::SamplerManager::get_default_sampler() const
{
    return default_sampler;
}

RBSampler vk::SamplerManager::get_or_create(const ::SamplerDesc& sampler_info)
{
    auto result = cache.find(sampler_info);
    if (result != cache.end())
        return result->second;
    
    vk::SamplerDesc desc(sampler_info);
    
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = desc.mag_filter;
    info.minFilter = desc.min_filter;
    info.mipmapMode = desc.mipmap_mode;
    info.mipLodBias = desc.min_lod_bias;

    info.addressModeU = desc.address_u;
    info.addressModeV = desc.address_v;
    info.addressModeW = desc.address_w;
    info.anisotropyEnable = desc.anisotropy;
    info.maxAnisotropy = desc.max_anisotropy;

    info.compareEnable = desc.comparison; 
    info.compareOp = desc.compare_op;

    info.minLod = desc.min_lod;
    info.maxLod = desc.max_lod;
    info.borderColor = desc.border_color;
    
    VkSampler sampler;

    vkCreateSampler(instance.get_device(), &info, nullptr, &sampler);
    
    LogVkSamplerManager.Log("Created sampler %s %p",
        std::string(sampler_info.name).c_str(), sampler);
    
    cache[sampler_info] = (RBSampler)sampler;
    
    return sampler;
}

