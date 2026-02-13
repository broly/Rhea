export module vk:sampler_desc;

import <vulkan/vulkan.h>;
import render;

VkFilter vk_convert_SamplerFilter(SamplerFilter v)
{
    switch (v)
    {
    case SamplerFilter::Nearest:
        return VK_FILTER_NEAREST;
    case SamplerFilter::Linear:
        return VK_FILTER_LINEAR;
    case SamplerFilter::CubicExt:
        return VK_FILTER_CUBIC_EXT;
    case SamplerFilter::CubicImg:
        return VK_FILTER_CUBIC_IMG; 
    default:
        return VK_FILTER_NEAREST;
    }
}


VkSamplerMipmapMode vk_convert_SamplerMipMapMode(SamplerMipMapMode v)
{
    switch (v)
    {
    case SamplerMipMapMode::Nearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case SamplerMipMapMode::Linear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    default:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
}

VkSamplerAddressMode vk_convert_SamplerAddressMode(SamplerAddressMode v)
{
    switch (v)
    {
    case SamplerAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case SamplerAddressMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case SamplerAddressMode::MirrorClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkCompareOp vk_convert_SamplerCompareOp(SamplerCompareOp v)
{
    switch (v)
    {
    case SamplerCompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case SamplerCompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case SamplerCompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case SamplerCompareOp::LessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case SamplerCompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case SamplerCompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case SamplerCompareOp::GreaterOrEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case SamplerCompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    default:
        return VK_COMPARE_OP_ALWAYS;
    }
}


VkBorderColor vk_convert_SamplerBorderColor(SamplerBorderColor v)
{
    switch (v)
    {
    case SamplerBorderColor::float_transparent_black:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case SamplerBorderColor::int_transparent_black:
        return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    case SamplerBorderColor::float_opaque_black:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case SamplerBorderColor::int_opaque_black:
        return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case SamplerBorderColor::float_opaque_white:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    case SamplerBorderColor::int_opaque_white:
        return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    default:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
}




namespace vk
{
    struct SamplerDesc
    {
        SamplerDesc(const ::SamplerDesc& abstract_desc)
        {
            min_filter   = vk_convert_SamplerFilter(abstract_desc.min_filter);
            mag_filter   = vk_convert_SamplerFilter(abstract_desc.mag_filter);
            mipmap_mode  = vk_convert_SamplerMipMapMode(abstract_desc.mipmap_mode);

            address_u = vk_convert_SamplerAddressMode(abstract_desc.address_u);
            address_v = vk_convert_SamplerAddressMode(abstract_desc.address_v);
            address_w = vk_convert_SamplerAddressMode(abstract_desc.address_w);

            min_lod_bias = abstract_desc.min_lod_bias;
            min_lod      = abstract_desc.min_lod;
            max_lod      = abstract_desc.max_lod;

            anisotropy      = abstract_desc.anisotropy ? VK_TRUE : VK_FALSE;
            max_anisotropy  = abstract_desc.max_anisotropy;

            comparison = abstract_desc.comparison ? VK_TRUE : VK_FALSE;
            compare_op = vk_convert_SamplerCompareOp(abstract_desc.compare_op);

            border_color = vk_convert_SamplerBorderColor(abstract_desc.border_color);
        }
        
        VkFilter min_filter;
        VkFilter mag_filter;
        VkSamplerMipmapMode mipmap_mode;

        VkSamplerAddressMode address_u;
        VkSamplerAddressMode address_v;
        VkSamplerAddressMode address_w;

        float min_lod_bias = 0.0f;
        float min_lod = 0.0f;
        float max_lod = 1000.0F;

        VkBool32 anisotropy = VK_FALSE;
        float max_anisotropy = 1.0f;

        VkBool32 comparison = VK_FALSE;
        VkCompareOp compare_op;

        VkBorderColor border_color;

        bool operator==(const SamplerDesc&) const = default;
    };
    
}