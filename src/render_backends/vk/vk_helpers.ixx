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

    inline VkFormat to_vk_format(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::RGB8:
            return VK_FORMAT_R8G8B8_UNORM;

        case TextureFormat::RGBA8:
            return VK_FORMAT_R8G8B8A8_UNORM;

        case TextureFormat::RGBA8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA16F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::RG16F:
            return VK_FORMAT_R16G16_SFLOAT;
        case TextureFormat::RGBA32F:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::Depth24Stencil8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::Depth32F:
            return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::Depth16UNorm:
            return VK_FORMAT_D16_UNORM;
        }
        return VK_FORMAT_UNDEFINED;
    }
    
    inline TextureFormat from_vk_format(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_R8G8B8_UNORM:
            return TextureFormat::RGB8;
        case VK_FORMAT_R8G8B8A8_SRGB:
            return TextureFormat::RGBA8_SRGB;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return TextureFormat::RGBA8_UNORM;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return TextureFormat::RGBA16F;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return TextureFormat::RGBA32F;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return TextureFormat::Depth24Stencil8;
        case VK_FORMAT_D32_SFLOAT:
            return TextureFormat::Depth32F;
        case VK_FORMAT_D16_UNORM:
            return TextureFormat::Depth16UNorm;
        }
        return TextureFormat::Undefined;
    }
    
#define VK_ENUM_CONVERT_TO_STRING_CASE(layout) \
    case layout: return #layout
    
#define VK_ENUM_MASK_APPEND(str, mask, value) \
    if ((mask & value) != 0) \
    { \
        str += " " #value; \
    }
    
    std::string pipeline_stage_flags_to_string(VkPipelineStageFlags mask)
    {
        std::string result;
        
        if (mask == 0)
            return "None";
        
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_TRANSFER_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_HOST_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_EXT)
        
        return result;
    }
    std::string access_flags_to_string(VkAccessFlags mask)
    {
        std::string result;
        
        if (mask == 0)
            return "None";
        
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_INDIRECT_COMMAND_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_INDEX_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_UNIFORM_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_SHADER_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_SHADER_WRITE_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_TRANSFER_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_TRANSFER_WRITE_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_HOST_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_HOST_WRITE_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_MEMORY_READ_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_MEMORY_WRITE_BIT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_COMMAND_PREPROCESS_READ_BIT_EXT)
        VK_ENUM_MASK_APPEND(result, mask, VK_ACCESS_COMMAND_PREPROCESS_WRITE_BIT_EXT)
        
        return result;
    }
    
    std::string_view enum_to_string(VkAttachmentLoadOp load_op)
    {
        switch (load_op)
        {
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_ATTACHMENT_LOAD_OP_LOAD);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_ATTACHMENT_LOAD_OP_CLEAR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        default:
            unreachable("invalid load_op");
        }
    }
    
    
    std::string_view enum_to_string(VkAttachmentStoreOp load_op)
    {
        switch (load_op)
        {
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_ATTACHMENT_STORE_OP_STORE);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_ATTACHMENT_STORE_OP_DONT_CARE);
        default:
            unreachable("invalid load_op");
        }
    }
    
    std::string_view enum_to_string(VkFormat format)
    {
        switch (format)
        {
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_UNDEFINED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R4G4_UNORM_PACK8);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R4G4B4A4_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B4G4R4A4_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R5G6B5_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B5G6R5_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R5G5B5A1_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B5G5R5A1_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A1R5G5B5_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8_SRGB);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8_SRGB);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8_SRGB);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8_SRGB);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8A8_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8A8_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8A8_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8A8_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8A8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8A8_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8G8B8A8_SRGB);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8A8_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8A8_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8A8_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8A8_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8A8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8A8_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8A8_SRGB);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A8B8G8R8_UNORM_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A8B8G8R8_SNORM_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A8B8G8R8_USCALED_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A8B8G8R8_SSCALED_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A8B8G8R8_UINT_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A8B8G8R8_SINT_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A8B8G8R8_SRGB_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2R10G10B10_UNORM_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2R10G10B10_SNORM_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2R10G10B10_USCALED_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2R10G10B10_SSCALED_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2R10G10B10_UINT_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2R10G10B10_SINT_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2B10G10R10_UNORM_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2B10G10R10_SNORM_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2B10G10R10_USCALED_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2B10G10R10_SSCALED_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2B10G10R10_UINT_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A2B10G10R10_SINT_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16A16_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16A16_SNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16A16_USCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16A16_SSCALED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16A16_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16A16_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16B16A16_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32G32_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32G32_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32G32_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32G32B32_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32G32B32_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32G32B32_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32G32B32A32_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32G32B32A32_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R32G32B32A32_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64G64_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64G64_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64G64_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64G64B64_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64G64B64_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64G64B64_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64G64B64A64_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64G64B64A64_SINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R64G64B64A64_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B10G11R11_UFLOAT_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_D16_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_X8_D24_UNORM_PACK32);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_D32_SFLOAT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_S8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_D16_UNORM_S8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_D24_UNORM_S8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_D32_SFLOAT_S8_UINT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC1_RGB_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC1_RGB_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC2_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC2_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC3_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC3_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC4_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC4_SNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC5_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC5_SNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC6H_UFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC6H_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC7_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_BC7_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_EAC_R11_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_EAC_R11_SNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_EAC_R11G11_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_EAC_R11G11_SNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_4x4_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_5x4_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_5x4_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_5x5_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_5x5_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_6x5_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_6x5_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_6x6_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_6x6_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_8x5_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_8x5_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_8x6_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_8x6_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_8x8_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_8x8_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x5_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x5_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x6_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x6_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x8_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x8_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x10_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x10_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_12x10_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_12x10_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_12x12_UNORM_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_12x12_SRGB_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G8B8G8R8_422_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B8G8R8G8_422_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G8_B8R8_2PLANE_422_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R10X6_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R10X6G10X6_UNORM_2PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R12X4_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R12X4G12X4_UNORM_2PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G16B16G16R16_422_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_B16G16R16G16_422_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G16_B16R16_2PLANE_420_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G16_B16R16_2PLANE_422_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G8_B8R8_2PLANE_444_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G16_B16R16_2PLANE_444_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A4R4G4B4_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A4B4G4R4_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A1B5G5R5_UNORM_PACK16);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_A8_UNORM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R8_BOOL_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R16G16_SFIXED5_NV);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R10X6_UINT_PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R10X6G10X6_UINT_2PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R10X6G10X6B10X6A10X6_UINT_4PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R12X4_UINT_PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R12X4G12X4_UINT_2PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R12X4G12X4B12X4A12X4_UINT_4PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R14X2_UINT_PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R14X2G14X2_UINT_2PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R14X2G14X2B14X2A14X2_UINT_4PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R14X2_UNORM_PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R14X2G14X2_UNORM_2PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_R14X2G14X2B14X2A14X2_UNORM_4PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G14X2_B14X2R14X2_2PLANE_420_UNORM_3PACK16_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_FORMAT_G14X2_B14X2R14X2_2PLANE_422_UNORM_3PACK16_ARM);
        default:
            unreachable("invalid format");
        }
    }

    
    std::string_view enum_to_string(VkImageLayout layout)
    {
        switch (layout)
        {
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_UNDEFINED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_GENERAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_PREINITIALIZED);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_VIDEO_ENCODE_DST_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_TENSOR_ALIASING_ARM);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_VIDEO_ENCODE_QUANTIZATION_MAP_KHR);
            VK_ENUM_CONVERT_TO_STRING_CASE(VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT);
        default:
            unreachable("wrong enum");
        };
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
