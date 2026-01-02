module vk:image_mgr;

import <vulkan/vulkan_core.h>;

#include "render_backends/vk/vk_macro.h"

import <cassert>;
import :helpers;

VkSampler vk::ImageManager::get_default_sampler() const
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

RBImageHandle vk::ImageManager::create_image_view(
    VkExtent2D extent, 
    const VkSurfaceFormatKHR& surface_format,
    VkImage image)
{
    VkImageView view;

    VkImageViewCreateInfo ivci{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
    };
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = surface_format.format;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(
        instance.device,
        &ivci,
        nullptr,
        &view));

    vk::ImageResource res{};
    res.image  = image;                 // VkImage
    res.view   = view;                  // VkImageView
    res.format = surface_format.format;
    res.width  = extent.width;
    res.height = extent.height;

    uint32_t id = static_cast<uint32_t>(image_resources.size());
    image_resources.push_back(res);
    
    return {id};
        
}

vk::ImageResource& vk::ImageManager::get_image_resource(RBImageHandle image_handle)
{
    return image_resources[image_handle.id];
}

VkImageView vk::ImageManager::get_image_view(RBImageHandle image_handle)
{
    return get_image_resource(image_handle).view;
}

RBImageHandle vk::ImageManager::create_image(const RBImageDesc& desc)
{
    uint32_t width  = desc.width;
    uint32_t height = desc.height;

    if (width == 0 || height == 0)
    {
        width  = default_width;
        height = default_height;
    }
    
    assert(width != 0 && height != 0);

    vk::ImageResource res{};
    res.width  = width;
    res.height = height;
    res.format = vk::to_vk_format(desc.format);

    VkImageUsageFlags vk_usage = 0;

    if (desc.usage & RenderTextureUsage::ColorAttachment)
        vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (desc.usage & RenderTextureUsage::DepthStencil)
        vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (desc.usage & RenderTextureUsage::Sampled)
        vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    
    if (desc.usage & RenderTextureUsage::TransferDst)
        vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkImageCreateInfo image_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = { width, height, 1 };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = vk::to_vk_format(desc.format);
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = vk_usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(instance.device, &image_info, nullptr, &res.image));

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(instance.device, res.image, &mem_req);

    VkMemoryAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex =
        vk::find_memory_type(
            instance.physical_device,
            mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(instance.device, &alloc_info, nullptr, &res.memory));
    VK_CHECK(vkBindImageMemory(instance.device, res.image, res.memory, 0));

    // ---- Image View ----
    VkImageAspectFlags aspect =
        (desc.usage & RenderTextureUsage::DepthStencil)
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo view_info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_info.image = res.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = vk::to_vk_format(desc.format);
    view_info.subresourceRange = {
        aspect, 0, 1, 0, 1
    };

    VK_CHECK(vkCreateImageView(instance.device, &view_info, nullptr, &res.view));

    uint32_t id = static_cast<uint32_t>(image_resources.size());
    image_resources.push_back(res);

    return RBImageHandle{ id };
}

void vk::ImageManager::set_default_extent(uint32_t width, uint32_t height)
{
    default_height = height;
    default_width = width;
}

VkFormat vk::ImageManager::get_image_format(RBImageHandle handle) const
{
    return image_resources[handle.id].format;
}
