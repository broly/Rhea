module vk:image_mgr;

import <vulkan/vulkan_core.h>;

#include "common/assertion_macros.h"
#include "render_backends/vk/vk_macro.h"

import reflect;
import <cassert>;
import :helpers;
import :log;
import :enums_adapters;
import :enums_to_string;


RBImageHandle vk::ImageManager::register_swapchain_image(
            VkExtent2D vk_extent, 
            const VkSurfaceFormatKHR& surface_format,
            VkImage image,
            std::optional<RBImageHandle> old_image_handle)
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
    
    uint32_t id = 0;
    
    if (!old_image_handle.has_value())
    {
        id = static_cast<uint32_t>(image_resources.size());
        vk::ImageResource res{};
        image_resources.push_back(res);
    }
    else
    {
        id = old_image_handle.value().id;
    }
    
    auto& res = image_resources[id];

    res.image  = image; 
    res.set_img_view(view);
    res.format = surface_format.format;
    res.extent.width  = vk_extent.width;
    res.extent.height = vk_extent.height;
    res.subresource_layouts.resize(1);
    res.subresource_layouts[0].resize(1, VK_IMAGE_LAYOUT_UNDEFINED);
    
    res.is_swapchain_image = true;
    
    
    return {id};
        
}

void vk::ImageManager::unregister_swapchain_image(RBImageHandle image_handle)
{
    auto& res = image_resources[image_handle.id];
    
    auto view = res.get_img_view();
    vkDestroyImageView(instance.device, view, nullptr);
    
    res.mark_destroyed();
}

RBImageView vk::ImageManager::fetch_image_view_generic(RBImageHandle image_handle, uint32_t layer_index, uint32_t mip_level, uint32_t num_mips,  bool is_cubemap)
{
    checkf(image_handle.id < image_resources.size(), "Unknown image id %i", image_handle.id);
    
    
    ImageResource& image_resource = image_resources[image_handle.id];
    
    if (is_cubemap && image_resource.has_cubemap())
        return image_resource.get_cubemap_view();
    else if (!is_cubemap && image_resource.has_view(layer_index, mip_level))
        return image_resource.get_img_view(layer_index, mip_level);
    
    checkf(image_resource.is_valid_view(layer_index, mip_level),
        "invalid layer_index or mip_level");
    
    VkImage image = image_resource.image;
    
    
    // ---- Image View ----
    VkImageAspectFlags aspect =
        (image_resource.usage & RenderTextureUsage::DepthStencil)
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo view_info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_info.image = image;
    view_info.viewType = is_cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = image_resource.format;
    view_info.subresourceRange = {
        aspect, 0, 1, 0, 1
    };
    view_info.subresourceRange.layerCount = is_cubemap ? 6 : 1;
    view_info.subresourceRange.baseArrayLayer = is_cubemap ? 0 : layer_index;
    view_info.subresourceRange.baseMipLevel = mip_level;
    view_info.subresourceRange.levelCount = num_mips == 0 ? image_resource.mip_levels : num_mips;
    if (is_cubemap)
    {
        view_info.subresourceRange.levelCount = 1;
    }
    

    VkImageView view;
    VK_CHECK(vkCreateImageView(instance.device, &view_info, nullptr, &view));
    
    if (!is_cubemap)
        image_resource.set_img_view(view, layer_index, mip_level);
    else
        image_resource.set_cubemap_img_view(view);
    

    return RBImageView(view);
}

vk::ImageResource& vk::ImageManager::get_image_resource(RBImageHandle image_handle)
{
    return image_resources[image_handle.id];
}

const vk::ImageResource& vk::ImageManager::get_image_resource(RBImageHandle image_handle) const
{
    return image_resources[image_handle.id];
}

VkImageView vk::ImageManager::get_view(RBImageHandle image_handle, uint32_t array_index, uint32_t mip_index)
{
    return get_image_resource(image_handle).get_img_view(array_index, mip_index);
}

VkImageView vk::ImageManager::get_cubemap_view(RBImageHandle image_handle)
{
    return fetch_image_view_generic(image_handle, 0, 0, 0, true);
}

RBImageHandle vk::ImageManager::create_image(const RBImageDesc& desc)
{
    Extent extent = desc.extent;
    if (extent.is_zero())
        extent = default_extent;
    
    assert(extent.is_not_zero());
    
    const bool use_existing_image = desc.old_image_handle.has_value();

    if (!use_existing_image)
    {
        image_resources.push_back({});
    }
    auto& res = use_existing_image ? image_resources[desc.old_image_handle.value().id] : image_resources.back();
    res.debug_name = desc.name;
    res.extent = extent;
    res.format = vk::to_vk_format(desc.format);
    res.mip_levels = desc.mip_levels;
    res.num_layers = desc.get_array_layers();
    res.usage = desc.usage;
    res.subresource_layouts.resize(res.num_layers);
    for (auto& layer : res.subresource_layouts)
    {
        layer.resize(res.mip_levels, VK_IMAGE_LAYOUT_UNDEFINED);
    }


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
    image_info.extent = { extent.width, extent.height, 1 };
    if (desc.is_cubemap)
    {
        image_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    image_info.mipLevels = desc.mip_levels;
    image_info.arrayLayers = desc.get_array_layers();
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
    
    uint32_t id;
    if (desc.old_image_handle.has_value())
    {
        id = desc.old_image_handle.value().id;
    }
    else
    {
        id = static_cast<uint32_t>(image_resources.size() - 1);
    }
    
    RBImageHandle img_handle { id };
    
    fetch_image_view_generic(img_handle, 0, 0, desc.use_mip_levels_for_image_view ? desc.mip_levels : 1);
    
    
    
    LogVkImageManager.Log("Created image%s %p (view[0]=%p) '%s' (array_layers=%i)", 
        desc.is_cubemap ? " [CUBEMAP]" : "",
        res.image, 
        res.get_img_view(0), 
        desc.name.to_string().c_str(),
        desc.get_array_layers());
    
    
    debug.register_vk_image_name(res.image, desc.name);

    return img_handle;
}

void vk::ImageManager::destroy_image(RBImageHandle handle, bool wait_fences)
{
    assert(handle.id < image_resources.size());
    
    if (wait_fences)
        vkDeviceWaitIdle(instance.device);

    vk::ImageResource& res = image_resources[handle.id];
    
    for (auto image_view : res.views)
    {
        vkDestroyImageView(instance.device, image_view, nullptr);
    }
    res.views.clear();

    if (res.image != VK_NULL_HANDLE)
    {
        vkDestroyImage(instance.device, res.image, nullptr);
        res.image = VK_NULL_HANDLE;
    }

    if (res.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(instance.device, res.memory, nullptr);
        res.memory = VK_NULL_HANDLE;
    }

    LogVkImageManager.Log(
        "Destroyed image %u", handle.id
    );
}

void vk::ImageManager::set_default_extent(Extent extent)
{
    default_extent = extent;
}

bool vk::ImageManager::is_swapchain_image(RBImageHandle image)
{
    return get_image_resource(image).is_swapchain_image;
}


RBImageHandle vk::ImageManager::create_texture_2d(const Texture& tex, const TextureCreationInfo& texture_creation_info)
{

    if (tex.extent.is_zero())
    {
        return create_fallback_texture(tex, texture_creation_info);
    }
    
    uint32_t mip_levels = texture_creation_info.generate_mips ? 
        static_cast<uint32_t>(std::floor(std::log2(std::max(tex.extent.width, tex.extent.height))) ) + 1 :
        1;
    
    RBImageDesc desc;
    desc.name = std::string("TEX_") + tex.name;
    desc.mip_levels = mip_levels;  // here
    desc.extent  = tex.extent;
    desc.format = texture_creation_info.format_override.value_or(tex.format);
    desc.usage  =
        RenderTextureUsage::Sampled |
        RenderTextureUsage::TransferDst |
        RenderTextureUsage::TransferSrc;
    desc.use_mip_levels_for_image_view = true;
    
    // 1. GPU image
    RBImageHandle image = create_image(desc);
    auto& res = get_image_resource(image);
    
    // 2. staging buffer
    size_t pixel_size =
        tex.format == TextureFormat::RGB8 ? 3 : 4;
    
    size_t upload_size =
        tex.extent.width * tex.extent.height * pixel_size;
    
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
    
    
    
    vk::update_buffer(instance.device, staging_memory, tex.bulk.data(), upload_size);
    
    immediate_command_pool.submit([&](VkCommandBuffer cmd)
    {
        ImageBarrierParams params_before_copy;
        params_before_copy.image = image;
        params_before_copy.before = RBImageLayout::undefined;
        params_before_copy.after = RBImageLayout::transfer_dst_optimal;
        transition_image(cmd, params_before_copy);
    
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageSubresource.mipLevel = 0;
        copy.imageExtent = {
            tex.extent.width,
            tex.extent.height,
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
        
        
        if (texture_creation_info.generate_mips)
        {
            generate_mipmaps(
                cmd,
                image,
                tex.extent.width,
                tex.extent.height,
                mip_levels
            );
        }
        if (texture_creation_info.current_layout != RBImageLayout::undefined)
        {
            ImageBarrierParams params_after_gen;
            params_after_gen.image = image;
            params_after_gen.before = RBImageLayout::transfer_dst_optimal;
            params_after_gen.after = texture_creation_info.current_layout;
            transition_image(cmd, params_after_gen);
        }
        // transition_image(
        //     cmd,
        //     image,
        //     RBImageUsage::TransferDst,
        //     RBImageUsage::SampledFragment
        // );
    });
    
    vk::destroy_buffer(instance.device, staging_buffer, staging_memory);
    
    LogVkImageManager.Log<Verbose>("Allocated GPU texture: %s", tex.name.c_str());
    
    return image;
}

RBImageHandle vk::ImageManager::create_fallback_texture(const Texture& tex,
    const TextureCreationInfo& texture_creation_info)
{
    
    const TextureFormat format =
        texture_creation_info.format_override.value_or(tex.format);
    

    // =========================
    // STATIC BLACK TEXTURE (FALLBACK)
    // =========================
    static std::unordered_map<TextureFormat, RBImageHandle> black_textures;
    
    auto it = black_textures.find(format);
    if (it != black_textures.end())
        return it->second;

    // --- create 1x1 black texture ---
    RBImageDesc desc;
    desc.name = std::string("TEX_B_") + tex.name;
    desc.extent.width  = 1;
    desc.extent.height = 1;
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
        ImageBarrierParams params_before_copy;
        params_before_copy.image = image;
        params_before_copy.before = RBImageLayout::undefined;
        params_before_copy.after = RBImageLayout::transfer_dst_optimal;
        transition_image(cmd, params_before_copy);

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
            
        ImageBarrierParams params_after_copy;
        params_after_copy.image = image;
        params_after_copy.before = RBImageLayout::transfer_dst_optimal;
        params_after_copy.after = RBImageLayout::shader_read_only_optimal;

        transition_image(cmd, params_after_copy); 
    });

    vk::destroy_buffer(
        instance.device,
        staging_buffer,
        staging_memory);

    black_textures[format] = image;
    return image;
}

RBImageHandle vk::ImageManager::create_cubemap(
    const Cubemap& cubemap,
    const TextureCreationInfo& texture_creation_info)
{
    const TextureFormat format =
        texture_creation_info.format_override.value_or(cubemap.format);

    const uint32_t mip_levels =
        static_cast<uint32_t>(cubemap.faces[0].size());

    if (cubemap.face_size == 0 || mip_levels == 0)
        throw std::runtime_error("Cubemap has invalid size");

    // =========================
    // IMAGE DESCRIPTION
    // =========================
    RBImageDesc desc;
    desc.name       = std::string("CUBEMAP_") + cubemap.name.to_string();
    desc.extent     = { cubemap.face_size, cubemap.face_size };
    desc.format     = format;
    desc.mip_levels = mip_levels;
    desc.is_cubemap = true;
    desc.array_layers = 6;
    
    desc.usage =
        RenderTextureUsage::Sampled |
        RenderTextureUsage::TransferDst;

    RBImageHandle image = create_image(desc);
    auto& res = get_image_resource(image);

    // =========================
    // STAGING SIZE
    // =========================
    const size_t pixel_size = bytes_per_pixel(format);

    size_t staging_size = 0;
    for (uint32_t mip = 0; mip < mip_levels; ++mip)
    {
        const uint32_t size = std::max(1u, cubemap.face_size >> mip);
        staging_size += 6 * size * size * pixel_size;
    }

    if (staging_size == 0)
        throw std::runtime_error("Cubemap staging_size == 0");

    // =========================
    // CREATE STAGING BUFFER
    // =========================
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;

    vk::create_buffer(
        instance.device,
        instance.physical_device,
        staging_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buffer,
        staging_memory);

    // =========================
    // BUILD CPU BLOB
    // =========================
    std::vector<uint8_t> blob;
    blob.resize(staging_size);

    std::vector<VkBufferImageCopy> copies;
    copies.reserve(6 * mip_levels);

    size_t offset = 0;

    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t mip = 0; mip < mip_levels; ++mip)
        {
            const uint32_t size = std::max(1u, cubemap.face_size >> mip);
            const auto& src = cubemap.faces[face][mip];

            const size_t bytes = size * size * pixel_size;

            memcpy(blob.data() + offset, src.data(), bytes);

            VkBufferImageCopy copy{};
            copy.bufferOffset = offset;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = mip;
            copy.imageSubresource.baseArrayLayer = face;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = { size, size, 1 };

            copies.push_back(copy);
            offset += bytes;
        }
    }

    // =========================
    // UPLOAD TO STAGING
    // =========================
    vk::update_buffer(
        instance.device,
        staging_memory,
        blob.data(),
        staging_size);

    // =========================
    // GPU COPY
    // =========================
    immediate_command_pool.submit([&](VkCommandBuffer cmd)
    {
        ImageBarrierParams params_before_copy;
        params_before_copy.image = image;
        params_before_copy.before = RBImageLayout::undefined;
        params_before_copy.after = RBImageLayout::transfer_dst_optimal;
        transition_image(cmd, params_before_copy);

        vkCmdCopyBufferToImage(
            cmd,
            staging_buffer,
            res.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(copies.size()),
            copies.data());

        ImageBarrierParams params_after_copy;
        params_after_copy.image = image;
        params_after_copy.before = RBImageLayout::transfer_dst_optimal;
        params_after_copy.after = texture_creation_info.current_layout != RBImageLayout::undefined
                ? texture_creation_info.current_layout
                : RBImageLayout::shader_read_only_optimal;
        transition_image(cmd, params_after_copy);
    });

    vk::destroy_buffer(instance.device, staging_buffer, staging_memory);

    LogVkImageManager.Log<Verbose>(
        "Allocated cubemap: %s (%u mips)",
        cubemap.name.to_string().c_str(),
        mip_levels);

    return image;
}

void vk::ImageManager::transition_image(RBCommandList cmd, const ImageBarrierParams& params) const
{
    auto [
        image, 
        before, 
        after, 
        base_layer,
        base_mip,
        layer_count,
        mip_count,
        log] = params;
    
    auto& img = get_image_resource(image);
    VkImage vk_img = img.image;
    
    
    if (layer_count == 0)
        layer_count = img.num_layers;
    
    if (mip_count == 0)
        mip_count = img.mip_levels;

    checkf(!img.destroyed, "Image destroyed");
    checkf(base_layer + layer_count <= img.num_layers, "Layer range OOB");
    checkf(base_mip   + mip_count   <= img.mip_levels, "Mip range OOB");

    if (img.is_swapchain_image)
    {
        checkf(before != RBImageLayout::undefined,
               "Swapchain image cannot transition from undefined");
    }

    auto src = vk::to_vk_state(before);
    auto dst = vk::to_vk_state(after);

    if (log)
    {
        LogVkImageManager.Log<Verbose>(
            "Transition '%s' (%p): %s -> %s | layers [%u..%u] mips [%u..%u]",
            debug.get_vk_image_name(vk_img).to_string().c_str(),
            vk_img,
            reflect::enum_name(before).to_string().c_str(),
            reflect::enum_name(after).to_string().c_str(),
            base_layer,
            base_layer + layer_count - 1,
            base_mip,
            base_mip + mip_count - 1
        );
    }

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

    barrier.oldLayout = src.layout;
    barrier.newLayout = dst.layout;
    barrier.srcAccessMask = src.access;
    barrier.dstAccessMask = dst.access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vk_img;

    if (vk::is_depth_format(img.format))
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        if (vk::has_stencil(img.format))
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    else
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    barrier.subresourceRange.baseMipLevel   = base_mip;
    barrier.subresourceRange.levelCount     = mip_count;
    barrier.subresourceRange.baseArrayLayer = base_layer;
    barrier.subresourceRange.layerCount     = layer_count;

    vkCmdPipelineBarrier(
        cmd.as<VkCommandBuffer>(),
        src.stage,
        dst.stage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    img.set_layout(dst.layout, params.base_layer, params.layer_count, params.base_mip, params.mip_count);

}

VkImageLayout vk::ImageManager::get_image_layout(RBImageHandle image, uint32_t layer, uint32_t mip)
{
    auto& image_res = get_image_resource(image);
    return image_res.get_layout(layer, mip);
}

void vk::ImageManager::create_staging_buffer(
    VkDeviceSize size,
    VkBuffer& out_buffer,
    VkDeviceMemory& out_memory) const
{
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size  = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(instance.device, &buffer_info, nullptr, &out_buffer));

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(instance.device, out_buffer, &mem_req);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        instance.physical_device,
        mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    VK_CHECK(vkAllocateMemory(instance.device, &alloc_info, nullptr, &out_memory));
    VK_CHECK(vkBindBufferMemory(instance.device, out_buffer, out_memory, 0));
}


void vk::ImageManager::copy_image_to_buffer(
    RBImageHandle img,
    std::vector<float>& out_buffer,
    TextureFormat& out_format,
    Extent extent)
{
    vk::ImageResource& img_resource = get_image_resource(img);
    const uint32_t array_dim = img_resource.num_layers;  // TODO
    
    VkImage image = img_resource.image;
    VkFormat format = get_image_format(img);

    uint32_t channels = 4;
    uint32_t bytes_per_channel = 0;

    switch (format)
    {
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            out_format = TextureFormat::RGBA16F;
            bytes_per_channel = 2;
            break;

        case VK_FORMAT_R32G32B32A32_SFLOAT:
            out_format = TextureFormat::RGBA32F;
            bytes_per_channel = 4;
            break;

        default:
            checkf(false, "Unsupported format for image readback");
            return;
    }

    const size_t pixel_count = extent.width * extent.height;
    const size_t gpu_byte_size =
        pixel_count * channels * bytes_per_channel;

    const size_t float_count =
        pixel_count * channels;

    out_buffer.resize(float_count);

    // ---------- Staging buffer ----------
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    create_staging_buffer(gpu_byte_size, staging_buffer, staging_memory);

    // ---------- Copy command ----------
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageExtent = {
        extent.width,
        extent.height,
        1
    };

    // submit + WAIT
    immediate_command_pool.submit([&](VkCommandBuffer cmd)
    {
        vkCmdCopyImageToBuffer(
            cmd,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging_buffer,
            1,
            &region
        );
    });

    // ---------- Map & convert ----------
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(
        instance.device,
        staging_memory,
        0,
        gpu_byte_size,
        0,
        &mapped
    ));

    if (format == VK_FORMAT_R32G32B32A32_SFLOAT)
    {
        memcpy(out_buffer.data(), mapped, gpu_byte_size);
    }
    else // RGBA16F → float
    {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(mapped);

        for (size_t i = 0; i < float_count; ++i)
            out_buffer[i] = math::half_to_float(src[i]);
    }

    vkUnmapMemory(instance.device, staging_memory);

    vkDestroyBuffer(instance.device, staging_buffer, nullptr);
    vkFreeMemory(instance.device, staging_memory, nullptr);
}



void vk::ImageManager::generate_mipmaps(VkCommandBuffer cmd, RBImageHandle image,
                                        uint32_t width, uint32_t height, uint32_t mip_levels)
{
    auto& res = get_image_resource(image);
    
    res.mip_levels = mip_levels;
    
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
    
        res.set_layout(barrier.newLayout, 0, 1, i, 1);
    }
    
}



VkImageSubresourceRange vk::ImageManager::full_subresource_range(RBImageHandle image) const
{
    const auto& img = get_image_resource(image);

    VkImageSubresourceRange range{};
    range.baseMipLevel   = 0;
    range.levelCount     = img.mip_levels;  // default = 1
    range.baseArrayLayer = 0;
    range.layerCount     = img.num_layers;  // default = 1

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

ImageReadback vk::ImageManager::readback(RBImageHandle img) const
{
    const vk::ImageResource& img_resource = get_image_resource(img);

    const uint32_t layers    = img_resource.num_layers;
    const uint32_t mip_count = img_resource.mip_levels;
    const Extent base_extent = img_resource.extent;

    VkImage  image  = img_resource.image;
    VkFormat format = img_resource.format;

    constexpr uint32_t channels = 4;

    uint32_t bytes_per_channel = 0;
    TextureFormat out_format;

    switch (format)
    {
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            out_format = TextureFormat::RGBA16F;
            bytes_per_channel = 2;
            break;

        case VK_FORMAT_R32G32B32A32_SFLOAT:
            out_format = TextureFormat::RGBA32F;
            bytes_per_channel = 4;
            break;

        default:
            checkf(false, "Unsupported format for image readback");
            return {};
    }

    auto mip_extent = [](const Extent& e, uint32_t mip) -> Extent
    {
        return {
            std::max(1u, e.width  >> mip),
            std::max(1u, e.height >> mip)
        };
    };

    // ------------------------------------------------------------
    // Calculate layout
    // ------------------------------------------------------------

    struct MipInfo
    {
        size_t offset;
        Extent extent;
        size_t byte_size;
    };

    std::vector<std::vector<MipInfo>> mip_infos(layers);
    size_t total_byte_size = 0;

    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        mip_infos[layer].resize(mip_count);

        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            Extent me = mip_extent(base_extent, mip);
            size_t pixel_count = me.width * me.height;
            size_t byte_size   = pixel_count * channels * bytes_per_channel;

            mip_infos[layer][mip] = {
                total_byte_size,
                me,
                byte_size
            };

            total_byte_size += byte_size;
        }
    }

    // ------------------------------------------------------------
    // Result
    // ------------------------------------------------------------

    ImageReadback result{};
    result.extent = base_extent;
    result.layers = layers;
    result.mips   = mip_count;
    result.format = out_format;

    result.data.resize(layers);

    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        result.data[layer].resize(mip_count);

        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            const Extent& me = mip_infos[layer][mip].extent;
            size_t pixel_count = me.width * me.height;
            result.data[layer][mip].resize(pixel_count * channels);
        }
    }

    // ------------------------------------------------------------
    // Staging buffer
    // ------------------------------------------------------------

    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    create_staging_buffer(total_byte_size, staging_buffer, staging_memory);

    // ------------------------------------------------------------
    // Copy regions
    // ------------------------------------------------------------

    std::vector<VkBufferImageCopy> regions;
    regions.reserve(layers * mip_count);

    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            const auto& info = mip_infos[layer][mip];

            VkBufferImageCopy region{};
            region.bufferOffset = info.offset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;

            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;

            region.imageExtent = {
                info.extent.width,
                info.extent.height,
                1
            };

            regions.push_back(region);
        }
    }

    immediate_command_pool.submit([&](VkCommandBuffer cmd)
    {
        ImageBarrierParams params;
        params.image = img;
        params.before = RBImageLayout::undefined;
        params.after = RBImageLayout::transfer_dst_optimal;
        transition_image(cmd, params);

        vkCmdCopyImageToBuffer(
            cmd,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging_buffer,
            static_cast<uint32_t>(regions.size()),
            regions.data()
        );
    });

    // ------------------------------------------------------------
    // Map & convert
    // ------------------------------------------------------------

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(
        instance.device,
        staging_memory,
        0,
        total_byte_size,
        0,
        &mapped
    ));

    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            const auto& info = mip_infos[layer][mip];

            uint8_t* src_bytes =
                reinterpret_cast<uint8_t*>(mapped) + info.offset;

            float* dst = result.data[layer][mip].data();

            size_t pixel_count = info.extent.width * info.extent.height;
            size_t float_count = pixel_count * channels;

            if (format == VK_FORMAT_R32G32B32A32_SFLOAT)
            {
                memcpy(dst, src_bytes, float_count * sizeof(float));
            }
            else // RGBA16F → float
            {
                const uint16_t* src =
                    reinterpret_cast<const uint16_t*>(src_bytes);

                for (size_t i = 0; i < float_count; ++i)
                    dst[i] = math::half_to_float(src[i]);
            }
        }
    }

    vkUnmapMemory(instance.device, staging_memory);

    vkDestroyBuffer(instance.device, staging_buffer, nullptr);
    vkFreeMemory(instance.device, staging_memory, nullptr);

    return result;
}


VkFormat vk::ImageManager::get_image_format(RBImageHandle handle) const
{
    return image_resources[handle.id].format;
}
