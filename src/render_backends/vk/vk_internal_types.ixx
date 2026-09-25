module;

#include <vulkan/vulkan_core.h>

export module vk:internal_types;

import std.compat;


namespace vk
{
    struct DescriptorSetLayoutBindingInfo
    {
        uint32_t array_size;
    };
    
    struct DescriptorSetLayoutData
    {
        VkDescriptorSetLayout vk_layout;
        uint32_t set_index;
        std::vector<DescriptorSetLayoutBindingInfo> bindings;
    };
}
