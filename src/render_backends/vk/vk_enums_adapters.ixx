export module vk:enums_adapters;

import <string>;
import <string_view>;
import <vulkan/vulkan_core.h>;

import render;
import texture_format;
#include "common/assertion_macros.h"

namespace vk
{
    
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
        case TextureFormat::R16F:
            return VK_FORMAT_R16_SFLOAT;
        case TextureFormat::R8F:
            return VK_FORMAT_R8_UNORM;
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
    
    inline VkPrimitiveTopology to_vk_primitive_topology(RBBufferTopology topology)
    {
        switch (topology) {
            case point_list: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case line_list: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case line_strip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case triangle_list: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case triangle_strip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case triangle_fan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
            case line_list_with_adjacency: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
            case line_strip_with_adjacency: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
            case triangle_list_with_adjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
            case triangle_strip_with_adjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
            case patch_list: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        }
        unreachable("wrong topology");
    }
    
}
