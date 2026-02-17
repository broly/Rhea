export module vk:helpers;
import <algorithm>;
import <span>;
import <stdexcept>;
import <vector>;
import <vulkan/vulkan_core.h>;

import texture_format;

import :context;

#include "vk_macro.h"
#include "common/assertion_macros.h"
import platform;
import render;
import assets;
import <GLFW/glfw3.h>;
import <cassert>;

export namespace vk
{
    VkAttachmentStoreOp vk_convert_attachment_store(RBStoreOp op)
    {
        switch (op)
        {
        case RBStoreOp::Store:
            return VK_ATTACHMENT_STORE_OP_STORE;
        case RBStoreOp::DontCare:
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default: ;
        }
        throw std::runtime_error("bad enum");
    }
    
    VkAttachmentLoadOp vk_convert_attachment_load(RBLoadOp op)
    {
        switch (op)
        {
        case RBLoadOp::Load:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case RBLoadOp::DontCare:
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        case RBLoadOp::Clear:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        throw std::runtime_error("bad enum");
    }
    
    
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

    void update_buffer(
        VkDevice device,
        VkDeviceMemory memory,
        const void* data,
        VkDeviceSize size,
        VkDeviceSize offset = 0)
    {
        void* mapped = nullptr;

        VK_CHECK(vkMapMemory(
            device,
            memory,
            offset,
            size,
            0,
            &mapped
        ));

        std::memcpy(mapped, data, static_cast<size_t>(size));

        vkUnmapMemory(device, memory);
    }
    
    void destroy_buffer(
        VkDevice device,
        VkBuffer buffer,
        VkDeviceMemory memory)
    {
        if (buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer, nullptr);

        if (memory != VK_NULL_HANDLE)
            vkFreeMemory(device, memory, nullptr);
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

            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(
                device, i, surface, &present_support);

            if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                present_support)
            {
                result.graphics = i;
                result.present  = i;
                break;
            }
        }
        return result;
    }

    inline VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats)
    {
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
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

        if (bool(stages & ShaderStage::vertex))
            flags |= VK_SHADER_STAGE_VERTEX_BIT;

        if (bool(stages & ShaderStage::fragment))
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

    inline bool is_depth_format(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }
    
    inline bool has_stencil(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }

    
    struct ImageState
    {
        VkImageLayout layout;
        VkPipelineStageFlags stage;
        VkAccessFlags access;
    };
    
    VkImageLayout to_attachment_layout(RBImageUsage usage, bool is_swapchain, bool final_layout)
    {
        switch (usage)
        {
        case RBImageUsage::ColorAttachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        case RBImageUsage::DepthStencilAttachment:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        case RBImageUsage::DepthStencilReadOnly:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        case RBImageUsage::SampledFragment:
        case RBImageUsage::SampledVertex:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        case RBImageUsage::TransferSrc:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        case RBImageUsage::TransferDst:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        case RBImageUsage::Present:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        case RBImageUsage::Undefined:
            return VK_IMAGE_LAYOUT_UNDEFINED;

        default:
            assert(false);
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }
    
    VkImageLayout to_subpass_layout(RBImageUsage usage)
    {
        switch (usage)
        {
        case RBImageUsage::ColorAttachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        case RBImageUsage::DepthStencilAttachment:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        case RBImageUsage::DepthStencilReadOnly:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        default:
            assert(false);
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    VkImageLayout to_final_layout(RBImageUsage usage, bool is_swapchain)
    {
        if (is_swapchain)
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        switch (usage)
        {
        case RBImageUsage::ColorAttachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        case RBImageUsage::DepthStencilAttachment:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        case RBImageUsage::DepthStencilReadOnly:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        case RBImageUsage::SampledFragment:
        case RBImageUsage::SampledVertex:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        default:
            return VK_IMAGE_LAYOUT_GENERAL;
        }
    }
    
    VkImageLayout to_initial_layout(RBLoadOp load, VkImageLayout current_layout)
    {
        if (load == RBLoadOp::Clear || load == RBLoadOp::DontCare)
            return VK_IMAGE_LAYOUT_UNDEFINED;

        return current_layout;
    }
    
    ImageState to_vk_state(RBImageLayout layout)
    {
        switch (layout)
        {
        case RBImageLayout::color_attachment_optimal:
            return {
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            };

        case RBImageLayout::depth_stencil_attachment_optimal:
            return {
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            };

        case RBImageLayout::depth_stencil_read_only_optimal:
            return {
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT
            };

        case RBImageLayout::shader_read_only_optimal:
            return {
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .stage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                .access = VK_ACCESS_SHADER_READ_BIT
            };

        case RBImageLayout::transfer_dst_optimal:
            return {
                .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_WRITE_BIT
            };
            
        case RBImageLayout::transfer_src_optimal:
            return {
                .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_READ_BIT
            };

        case RBImageLayout::transfer_present:
            return {
                .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .access = 0
            };

        case RBImageLayout::undefined:
            return {
                .layout = VK_IMAGE_LAYOUT_UNDEFINED,
                .stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                .access = 0
            };
        default:
            unreachable("wrong code path");
        }
    }
    
    uint32_t bytes_per_pixel(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_UNORM:
            return 4;

        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;

        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16;

        default:
            assert(false && "Unsupported format");
            return 0;
        }
    }


}
