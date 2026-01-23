module vk:image_mgr;

import <vulkan/vulkan_core.h>;

#include "render_backends/vk/vk_macro.h"

import <cassert>;
import :helpers;


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
    ivci.subresourceRange.baseMipLevel = 0;
    ivci.subresourceRange.layerCount = 1;
    ivci.subresourceRange.baseArrayLayer = 0;

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
    res.mip_levels = desc.mip_levels;

    VkImageUsageFlags vk_usage = 0;

    if (desc.usage & RenderTextureUsage::ColorAttachment)
        vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (desc.usage & RenderTextureUsage::DepthStencil)
        vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (desc.usage & RenderTextureUsage::Sampled)
        vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    
    if (desc.usage & RenderTextureUsage::TransferDst)
        vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    if (desc.usage & RenderTextureUsage::TransferSrc)
        vk_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkImageCreateInfo image_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = { width, height, 1 };
    image_info.mipLevels = desc.mip_levels;
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
        aspect, 0, desc.mip_levels, 0, 1
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


RBImageHandle vk::ImageManager::create_texture_2d(const Texture& tex, std::optional<TextureFormat> format_override)
{
    const TextureFormat format =
        format_override.value_or(tex.format);
    

    // =========================
    // STATIC BLACK TEXTURE (FALLBACK)
    // =========================
    static std::unordered_map<TextureFormat, RBImageHandle> black_textures;

    if (tex.width == 0 || tex.height == 0)
    {
        auto it = black_textures.find(format);
        if (it != black_textures.end())
            return it->second;

        // --- create 1x1 black texture ---
        RBImageDesc desc;
        desc.width  = 1;
        desc.height = 1;
        desc.format = format;
        desc.usage =
            RenderTextureUsage::Sampled |
            RenderTextureUsage::TransferDst;

        RBImageHandle image = create_image(desc);
        auto& res = get_image_resource(image);

        const size_t pixel_size =
            format == TextureFormat::RGB8 ? 3 : 4;

        uint8_t black_pixel[4] = { 0, 0, 0, 255 };

        VkBuffer staging_buffer;
        VkDeviceMemory staging_memory;

        vk::create_buffer(
            instance.device,
            instance.physical_device,
            pixel_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging_buffer,
            staging_memory);

        vk::update_buffer(
            instance.device,
            staging_memory,
            black_pixel,
            pixel_size);

        immediate_command_pool.submit([&](VkCommandBuffer cmd)
        {
            transition_image(
                cmd,
                image,
                RBImageLayout::undefined,
                RBImageLayout::transfer_dst_optimal);

            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = { 1, 1, 1 };

            vkCmdCopyBufferToImage(
                cmd,
                staging_buffer,
                res.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy);
            

            transition_image(
                cmd,
                image,
                RBImageLayout::transfer_dst_optimal,
                RBImageLayout::shader_read_only_optimal); 
        });

        vk::destroy_buffer(
            instance.device,
            staging_buffer,
            staging_memory);

        black_textures[format] = image;
        return image;
    }
    
    uint32_t mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(tex.width, tex.height))) ) + 1;
    
    RBImageDesc desc;
    desc.mip_levels = mip_levels;  // here
    desc.width  = tex.width;
    desc.height = tex.height;
    desc.format = format_override.value_or(tex.format);
    desc.usage  =
        RenderTextureUsage::Sampled |
        RenderTextureUsage::TransferDst |
        RenderTextureUsage::TransferSrc;
    
    // 1. GPU image
    RBImageHandle image = create_image(desc);
    auto& res = get_image_resource(image);
    
    // 2. staging buffer
    size_t pixel_size =
        tex.format == TextureFormat::RGB8 ? 3 : 4;
    
    size_t upload_size =
        tex.width * tex.height * pixel_size;
    
    if (upload_size == 0)
    {
        throw std::runtime_error("create_texture_2d: upload_size == 0");
    }
    
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    
    vk::create_buffer(
        instance.device,
        instance.physical_device,
        upload_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buffer,
        staging_memory);
    
    
    
    vk::update_buffer(instance.device, staging_memory, tex.pixels.data(), upload_size);
    
    // 3. copy
    immediate_command_pool.submit([&](VkCommandBuffer cmd)
    {
        transition_image(
            cmd,
            image,
            RBImageLayout::undefined,
            RBImageLayout::transfer_dst_optimal
        );
    
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageSubresource.mipLevel = 0;
        copy.imageExtent = {
            tex.width,
            tex.height,
            1
        };
    
        vkCmdCopyBufferToImage(
            cmd,
            staging_buffer,
            res.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy
        );
        
        
        generate_mipmaps(
            cmd,
            image,
            tex.width,
            tex.height,
            mip_levels
        );
    
        // transition_image(
        //     cmd,
        //     image,
        //     RBImageUsage::TransferDst,
        //     RBImageUsage::SampledFragment
        // );
    });
    
    vk::destroy_buffer(instance.device, staging_buffer, staging_memory);
    
    return image;
}

void vk::ImageManager::transition_image(RBCommandList cmd, RBImageHandle image, RBImageLayout before, RBImageLayout after)
{
    auto src = vk::to_vk_state(before);
    auto dst = vk::to_vk_state(after);

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = src.layout;
    barrier.newLayout = dst.layout;
    barrier.srcAccessMask = src.access;
    barrier.dstAccessMask = dst.access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = get_image_resource(image).image;

    barrier.subresourceRange = full_subresource_range(image);

    vkCmdPipelineBarrier(
        cmd.as<VkCommandBuffer>(),
        src.stage,
        dst.stage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void vk::ImageManager::generate_mipmaps(VkCommandBuffer cmd, RBImageHandle image,
                                        uint32_t width, uint32_t height, uint32_t mip_levels)
{
    auto& res = get_image_resource(image);
    VkImage img = res.image;

    int32_t mipWidth  = static_cast<int32_t>(width);
    int32_t mipHeight = static_cast<int32_t>(height);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = img;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    for (uint32_t i = 1; i < mip_levels; i++)
    {
        // mip i-1 → TRANSFER_SRC
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Blit mip i-1 → i
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = { std::max(mipWidth / 2, 1),
                               std::max(mipHeight / 2, 1),
                               1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd,
                       img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit,
                       VK_FILTER_LINEAR);

        mipWidth  = std::max(mipWidth / 2, 1);
        mipHeight = std::max(mipHeight / 2, 1);
    }

    for (uint32_t i = 0; i < mip_levels; i++)
    {
        barrier.subresourceRange.baseMipLevel = i;
        barrier.oldLayout = (i == mip_levels - 1) ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                                                  : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        barrier.srcAccessMask = (i == mip_levels - 1) ? VK_ACCESS_TRANSFER_WRITE_BIT
                                                     : VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}



VkImageSubresourceRange vk::ImageManager::full_subresource_range(RBImageHandle image)
{
    const auto& img = get_image_resource(image);

    VkImageSubresourceRange range{};
    range.baseMipLevel   = 0;
    range.levelCount     = img.mip_levels;  // default = 1
    range.baseArrayLayer = 0;
    range.layerCount     = img.array_layers;  // default = 1

    if (vk::is_depth_format(img.format))
    {
        range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        if (vk::has_stencil(img.format))
            range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    else
    {
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    return range;
}

VkFormat vk::ImageManager::get_image_format(RBImageHandle handle) const
{
    return image_resources[handle.id].format;
}
