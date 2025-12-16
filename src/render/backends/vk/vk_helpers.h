#pragma once
#include <vulkan/vulkan_core.h>

#include "vk_macro.h"

namespace vk
{
    inline uint32_t find_memory_type(
        VkPhysicalDevice physicalDevice,
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(
            physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type!");
    }

    
    inline void create_buffer(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory)
    {
        // 1. Create buffer
        VkBufferCreateInfo bufferInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
        };
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK(vkCreateBuffer(
            device, &bufferInfo,
            nullptr, &buffer));

        // 2. Memory requirements
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(
            device, buffer, &memRequirements);

        // 3. Allocate memory
        VkMemoryAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        };
        allocInfo.allocationSize =
            memRequirements.size;
        allocInfo.memoryTypeIndex =
            find_memory_type(
                physicalDevice,
                memRequirements.memoryTypeBits,
                properties);

        VK_CHECK(vkAllocateMemory(
            device, &allocInfo,
            nullptr, &bufferMemory));

        // 4. Bind
        VK_CHECK(vkBindBufferMemory(
            device, buffer,
            bufferMemory, 0));
    }
}
