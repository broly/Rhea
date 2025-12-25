#pragma once
#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace vk
{
    struct DescriptorSetLayoutData
    {
        VkDescriptorSetLayout vk_layout;
        uint32_t set_index;
    };
}
