#pragma once
#include <algorithm>
#include <span>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "vk_context.h"
#include "vk_macro.h"
#include "platform/window.h"
#include "render/graphics_pipeline.h"

namespace vk
{
    
    struct SwapchainSupport {
        std::vector<VkPresentModeKHR> present_modes;
        std::vector<VkSurfaceFormatKHR> formats;
        VkSurfaceCapabilitiesKHR caps;
    };
    
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


    inline SwapchainSupport query_swapchain_support(
        VkPhysicalDevice device,
        VkSurfaceKHR surface)
    {
        SwapchainSupport s;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            device, surface, &s.caps);

        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface, &count, nullptr);

        s.formats.resize(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface, &count, s.formats.data());

        count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &count, nullptr);

        s.present_modes.resize(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &count, s.present_modes.data());

        return s;
    }


    inline QueueFamilies find_queue_families(
        VkPhysicalDevice device,
        VkSurfaceKHR surface)
    {
        QueueFamilies result;

        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

        std::vector<VkQueueFamilyProperties> props(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props.data());

        for (uint32_t i = 0; i < count; i++) {
            if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                result.graphics = i;

            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(
                device, i, surface, &present_support);

            if (present_support)
                result.present = i;
        }

        return result;
    }

    inline VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats)
    {
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return f;
        }
        return formats[0];
    }

    inline VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes)
    {
        for (auto m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
                return m; // triple buffering
        }
        return VK_PRESENT_MODE_FIFO_KHR; 
    }

    inline VkExtent2D choose_extent(
        const VkSurfaceCapabilitiesKHR& caps,
        GLFWwindow* window)
    {
        if (caps.currentExtent.width != UINT32_MAX)
            return caps.currentExtent;

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);

        VkExtent2D extent{
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h)
        };

        extent.width = std::clamp(
            extent.width,
            caps.minImageExtent.width,
            caps.maxImageExtent.width);

        extent.height = std::clamp(
            extent.height,
            caps.minImageExtent.height,
            caps.maxImageExtent.height);

        return extent;
    }


    inline VkDescriptorType to_vk_descriptor_type(DescriptorType type)
    {
        switch (type)
        {
        case DescriptorType::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

        case DescriptorType::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        case DescriptorType::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;

        case DescriptorType::SampledImage:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

        case DescriptorType::CombinedImageSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

        default:
            assert(false);
            return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }
    }


    inline VkShaderStageFlags to_vk_shader_stage_flags(ShaderStage stages)
    {
        VkShaderStageFlags flags = 0;

        if (stages & ShaderStage::ss_Vertex)
            flags |= VK_SHADER_STAGE_VERTEX_BIT;

        if (stages & ShaderStage::ss_Fragment)
            flags |= VK_SHADER_STAGE_FRAGMENT_BIT;

        return flags;
    }
    
    inline std::vector<VkPushConstantRange> to_vk_ranges(std::span<const PushConstantRange> ranges)
    {
        std::vector<VkPushConstantRange> vk;
        vk.reserve(ranges.size());

        for (const PushConstantRange& r : ranges)
        {
            VkPushConstantRange vk_range{};
            vk_range.stageFlags = vk::to_vk_shader_stage_flags(r.stages);
            vk_range.offset     = r.offset;
            vk_range.size       = r.size;

            vk.push_back(vk_range);
        }

        return vk;
    }
    
    
    inline VkFormat get_vk_format(RGTextureFormat format)
    {
        switch (format)
        {
        case RGTextureFormat::RGBA8_UNORM:
            return VK_FORMAT_B8G8R8A8_UNORM;

        case RGTextureFormat::RGBA8_SRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;

        case RGTextureFormat::RGBA16F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;

        case RGTextureFormat::RGBA32F:
            return VK_FORMAT_R32G32B32A32_SFLOAT;

        case RGTextureFormat::Depth24Stencil8:
            return VK_FORMAT_D24_UNORM_S8_UINT;

        case RGTextureFormat::Depth32F:
            return VK_FORMAT_D32_SFLOAT;

        case RGTextureFormat::Undefined:
        default:
            throw std::runtime_error("Unsupported texture format");
        }
    }


}
