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
            uint32_t image_index,
            std::optional<RBImageHandle> old_image_handle)
{
    
    LogVkImageManager.Log("Register swapchain image: %p", image);
    VkImageView view;
    
    Name image_name = std::string("[SWAPCHAIN_IMAGE_") + std::to_string(image_index) + "]";
    Name image_view_name = std::string("[SWAPCHAIN_IMAGE_") + std::to_string(image_index) + "_VIEW]";

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
    
    
    debug_object_tracker.register_object((uint64_t)image, image_name);

    VK_CHECK(vkCreateImageView(
        instance.device,
        &ivci,
        nullptr,
        &view));
    
    debug_object_tracker.register_object((uint32_t)view, image_view_name);
    
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
    res.debug_name = image_name;
    res.set_img_view(view);
    res.format = surface_format.format;
    res.extent.width  = vk_extent.width;
    res.extent.height = vk_extent.height;
    res.num_layers = 1;
    res.init_states();
    
    
    
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

RBImageView vk::ImageManager::fetch_image_view_generic(RBImageHandle image_handle, 
    uint32_t layer_index, uint32_t mip_level, 
    uint32_t num_mips, uint32_t num_layers, bool is_cubemap, bool as_array_2d)
{
    checkf(image_handle.id < image_resources.size(), "Unknown image id %i", image_handle.id);
    
    
    ImageResource& image_resource = image_resources[image_handle.id];
    
    // ---------------------------------------------------------------
    // View type selection.
    //
    // Three modes:
    //   - cubemap       -> VIEW_TYPE_CUBE, layerCount=6
    //   - as_array_2d   -> VIEW_TYPE_2D_ARRAY, layerCount=num_layers
    //   - default       -> VIEW_TYPE_2D, layerCount=1
    //
    // Why as_array_2d exists:
    //   We have two distinct array-texture use-cases:
    //
    //   1) ping-pong history textures (num_layers=2). The shader binding
    //      is a flat Sampler2D / RWTexture2D, and per frame we update the
    //      descriptor to point at a *single-layer* view (layer 0 vs
    //      layer 1). This needs VIEW_TYPE_2D, layerCount=1.
    //
    //   2) NN-denoiser depthwise weights (num_layers up to 64). The
    //      shader binding is `Sampler2DArray u_nn_dw_weights[N]` and
    //      samples the entire array as one bound view via Load(int4(...)).
    //      This needs VIEW_TYPE_2D_ARRAY, layerCount=num_layers.
    //
    // Auto-detection on num_layers alone cannot distinguish these. The
    // caller must opt in explicitly.
    //
    // Mismatch is the root cause of the intermittent VK_ERROR_DEVICE_LOST
    // we saw with the denoiser enabled: a Sampler2DArray binding fed a
    // VIEW_TYPE_2D image view, NVIDIA driver tolerated it for a while and
    // then crashed. The post-mortem validation noise (vkResetFences in
    // use, semaphore pending operations, command buffers in pending state)
    // is downstream of the lost device.
    // ---------------------------------------------------------------
    checkf(!(is_cubemap && as_array_2d), "as_array_2d and is_cubemap are mutually exclusive");

    if (is_cubemap && image_resource.has_cubemap())
        return image_resource.get_cubemap_view();
    else if (!is_cubemap && image_resource.has_view(layer_index, mip_level) && !as_array_2d)
        return image_resource.get_img_view(layer_index, mip_level);
    
    if (!as_array_2d)
    {
        checkf(image_resource.is_valid_view(layer_index, mip_level),
            "invalid layer_index or mip_level");
    }
    else
    {
        checkf(image_resource.num_layers > 0, "as_array_2d requires array image");
        checkf(layer_index == 0, "as_array_2d view must start at layer 0");
    }
    
    VkImage image = image_resource.image;
    
    
    // ---- Image View ----
    VkImageAspectFlags aspect =
        (is_depth_format(image_resource.format))
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo view_info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_info.image = image;

    if (is_cubemap)
        view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    else if (as_array_2d)
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    else
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;

    view_info.format = image_resource.format;
    view_info.subresourceRange = {
        aspect, 0, 1, 0, 1
    };
    
    uint32_t layer_count;
    if (is_cubemap)
        layer_count = 6;
    else if (as_array_2d)
        layer_count = image_resource.num_layers;
    else
        layer_count = 1;

    const uint32_t mip_level_count = num_mips == 0 ? image_resource.mip_levels : num_mips;
    const uint32_t array_layer_index = is_cubemap ? 0 : layer_index;
    
    view_info.subresourceRange.layerCount = layer_count;
    view_info.subresourceRange.baseArrayLayer = array_layer_index;
    view_info.subresourceRange.baseMipLevel = mip_level;
    view_info.subresourceRange.levelCount = mip_level_count;
    if (is_cubemap)
    {
        view_info.subresourceRange.levelCount = 1;
    }
    

    VkImageView view;
    VK_CHECK(vkCreateImageView(instance.device, &view_info, nullptr, &view));
    
    const std::string array_subresources = layer_count == 1 ? 
        std::string("[") + std::to_string(array_layer_index) + "]" :
        std::string("[") + std::to_string(array_layer_index) + ".." + std::to_string(array_layer_index + layer_count) + "]";
    
    const std::string mip_subresources = mip_level_count == 1 ? 
        std::string("[") + std::to_string(mip_level) + "]" :
        std::string("[") + std::to_string(mip_level) + ".." + std::to_string(mip_level + mip_level_count) + "]";
    
    const Name image_view_name = image_resource.debug_name.to_string() + array_subresources + mip_subresources;
    debug_object_tracker.register_object((uint64_t)view, image_view_name);
  
    if (!is_cubemap && !as_array_2d)
        image_resource.set_img_view(view, layer_index, mip_level);
    else if (as_array_2d)
        image_resource.set_array_view(view);
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

VkImageView vk::ImageManager::get_view(RBImageHandle image_handle, uint32_t layer_index, uint32_t mip_index)
{
    return fetch_image_view_generic(image_handle, layer_index, mip_index);
}

VkImageView vk::ImageManager::get_array_view(RBImageHandle image_handle, uint32_t layer_index, uint32_t num_layers)
{
    // 2D_ARRAY view spanning all layers from baseArrayLayer=0.
    // Use this for shader bindings declared as Sampler2DArray /
    // RWTexture2DArray, where the entire array is sampled by one bind.
    return fetch_image_view_generic(
        image_handle,
        /*layer_index=*/layer_index,
        /*mip_level=*/0,
        /*num_mips=*/0,         // = use image's mip_levels
        /*num_layers*/num_layers,
        /*is_cubemap=*/false,
        /*as_array_2d=*/true);
}

VkImageView vk::ImageManager::get_cubemap_view(RBImageHandle image_handle)
{
    return fetch_image_view_generic(image_handle, 0, 0, 0, true);
}


vk::ImageManager::PendingReadback vk::ImageManager::enqueue_readback(RBCommandList cmd_list, RBImageHandle img)
{
    const vk::ImageResource& img_resource = get_image_resource(img);

    const uint32_t layers    = img_resource.num_layers;
    const uint32_t mip_count = img_resource.mip_levels;
    const Extent base_extent = img_resource.extent;

    VkImage  image  = img_resource.image;
    VkFormat format = img_resource.format;

    // ---------- Format info ----------
    PendingReadback pending{};
    pending.base_extent = base_extent;
    pending.layers      = layers;
    pending.mips        = mip_count;

    uint32_t bytes_per_channel = 0;

    switch (format)
    {
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            pending.out_format = TextureFormat::RGBA16F;
            pending.channels = 4; bytes_per_channel = 2;
            pending.component_type = PendingReadback::ComponentType::Float16;
            break;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            pending.out_format = TextureFormat::RGBA32F;
            pending.channels = 4; bytes_per_channel = 4;
            pending.component_type = PendingReadback::ComponentType::Float32;
            break;
        case VK_FORMAT_R32_SFLOAT:
            pending.out_format = TextureFormat::R32F;
            pending.channels = 1; bytes_per_channel = 4;
            pending.component_type = PendingReadback::ComponentType::Float32;
            break;
        case VK_FORMAT_R16G16_SFLOAT:
            pending.out_format = TextureFormat::RG16F;
            pending.channels = 2; bytes_per_channel = 2;
            pending.component_type = PendingReadback::ComponentType::Float16;
            break;
        case VK_FORMAT_R8G8B8A8_UNORM:
            pending.out_format = TextureFormat::RGBA8_UNORM;
            pending.channels = 4; bytes_per_channel = 1;
            pending.component_type = PendingReadback::ComponentType::Unorm8;
            break;
        default:
            checkf(false, "Unsupported format for image readback: %i", int(format));
            return pending;
    }

    // ---------- Mip layout ----------
    pending.mip_infos.resize(layers);
    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        pending.mip_infos[layer].resize(mip_count);
        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            Extent me = ImageReadback::mip_extent(base_extent, mip);
            size_t pixel_count = size_t(me.width) * me.height;
            size_t byte_size   = pixel_count * pending.channels * bytes_per_channel;

            pending.mip_infos[layer][mip] = { pending.total_byte_size, me, byte_size };
            pending.total_byte_size += byte_size;
        }
    }

    // ---------- Staging buffer ----------
    create_staging_buffer(pending.total_byte_size, pending.buffer, pending.memory);

    // ---------- Copy regions ----------
    std::vector<VkBufferImageCopy> regions;
    regions.reserve(layers * mip_count);

    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            const auto& mi = pending.mip_infos[layer][mip];

            VkBufferImageCopy region{};
            region.bufferOffset = mi.offset;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { mi.extent.width, mi.extent.height, 1 };
            regions.push_back(region);
        }
    }

    VkCommandBuffer vk_cmd = cmd_list.as<VkCommandBuffer>();

    vkCmdCopyImageToBuffer(
        vk_cmd,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        pending.buffer,
        static_cast<uint32_t>(regions.size()),
        regions.data()
    );

    return pending;
}

ImageReadback vk::ImageManager::finalize_readback(PendingReadback&& pending) const
{
    ImageReadback result{};
    result.extent   = pending.base_extent;
    result.layers   = pending.layers;
    result.mips     = pending.mips;
    result.format   = pending.out_format;
    result.channels = pending.channels;

    result.data.resize(pending.layers);
    for (uint32_t layer = 0; layer < pending.layers; ++layer)
    {
        result.data[layer].resize(pending.mips);
        for (uint32_t mip = 0; mip < pending.mips; ++mip)
        {
            const Extent& me = pending.mip_infos[layer][mip].extent;
            size_t pixel_count = size_t(me.width) * me.height;
            result.data[layer][mip].resize(pixel_count * pending.channels);
        }
    }

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(instance.device, pending.memory, 0, pending.total_byte_size, 0, &mapped));

    for (uint32_t layer = 0; layer < pending.layers; ++layer)
    {
        for (uint32_t mip = 0; mip < pending.mips; ++mip)
        {
            const auto& mi = pending.mip_infos[layer][mip];
            const uint8_t* src_bytes = reinterpret_cast<const uint8_t*>(mapped) + mi.offset;
            float* dst = result.data[layer][mip].data();
            const size_t pixel_count = size_t(mi.extent.width) * mi.extent.height;
            const size_t value_count = pixel_count * pending.channels;

            switch (pending.component_type)
            {
                case PendingReadback::ComponentType::Float32:
                    memcpy(dst, src_bytes, value_count * sizeof(float));
                    break;
                case PendingReadback::ComponentType::Float16:
                {
                    const uint16_t* src = reinterpret_cast<const uint16_t*>(src_bytes);
                    for (size_t i = 0; i < value_count; ++i)
                        dst[i] = math::half_to_float(src[i]);
                    break;
                }
                case PendingReadback::ComponentType::Unorm8:
                {
                    const uint8_t* src = src_bytes;
                    constexpr float inv_255 = 1.0f / 255.0f;
                    for (size_t i = 0; i < value_count; ++i)
                        dst[i] = float(src[i]) * inv_255;
                    break;
                }
            }
        }
    }

    vkUnmapMemory(instance.device, pending.memory);
    vkDestroyBuffer(instance.device, pending.buffer, nullptr);
    vkFreeMemory(instance.device, pending.memory, nullptr);

    return result;
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
    res.num_layers = desc.get_num_layers();
    res.usage = desc.usage;
    res.init_states();


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
    
    if (desc.usage & RenderTextureUsage::Storage)
        vk_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    

    VkImageCreateInfo image_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = { extent.width, extent.height, 1 };
    if (desc.is_cubemap)
    {
        image_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    image_info.mipLevels = desc.mip_levels;
    image_info.arrayLayers = desc.get_num_layers();
    image_info.format = vk::to_vk_format(desc.format);
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = vk_usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    

    VK_CHECK(vkCreateImage(instance.device, &image_info, nullptr, &res.image));
    
    
    debug_object_tracker.register_object((uint64_t)res.image, desc.name);
    
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
        desc.get_num_layers());
    
    
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
        return create_fallback_texture(tex, texture_creation_info);

    const uint32_t array_layers = std::max(1u, texture_creation_info.array_layers);
    const bool is_array = array_layers > 1;


    checkf(!texture_creation_info.generate_mips || !is_array,
           "Mip generation for 2D arrays not implemented");

    uint32_t mip_levels = texture_creation_info.generate_mips ?
        static_cast<uint32_t>(std::floor(std::log2(std::max(tex.extent.width, tex.extent.height)))) + 1 :
        1;

    RBImageDesc desc;
    desc.name = std::string("TEX_") + tex.name;
    desc.mip_levels = mip_levels;
    desc.num_layers = array_layers;
    desc.extent = tex.extent;
    desc.format = texture_creation_info.format_override.value_or(tex.format);
    desc.usage =
        RenderTextureUsage::Sampled |
        RenderTextureUsage::TransferDst |
        RenderTextureUsage::TransferSrc;
    desc.use_mip_levels_for_image_view = true;

    RBImageHandle image = create_image(desc);
    auto& res = get_image_resource(image);


    size_t pixel_size;
    switch (desc.format)
    {
        case TextureFormat::RGB8:    pixel_size = 3;  break;
        case TextureFormat::RGBA8:   pixel_size = 4;  break;
        case TextureFormat::RGBA16F: pixel_size = 8;  break;
        case TextureFormat::RGBA32F: pixel_size = 16; break;
        default:
            throw std::runtime_error("create_texture_2d: unsupported format");
    }

    const size_t per_layer_size = tex.extent.width * tex.extent.height * pixel_size;
    const size_t upload_size = per_layer_size * array_layers;

    if (upload_size == 0)
        throw std::runtime_error("create_texture_2d: upload_size == 0");

    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;

    vk::create_buffer(
        instance.device, instance.physical_device,
        upload_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buffer, staging_memory);

    vk::update_buffer(instance.device, staging_memory, tex.bulk.data(), upload_size);

    immediate_command_pool.submit([&](VkCommandBuffer cmd)
    {
        ImageBarrierParams params_before_copy;
        params_before_copy.image = image;
        params_before_copy.src_usage = RBImageUsageType::Undefined;
        params_before_copy.dst_usage = RBImageUsageType::TransferDst;
        transition_image(cmd, params_before_copy);

        VkBufferImageCopy copy{};
        copy.bufferOffset = 0;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = array_layers;
        copy.imageOffset = {0, 0, 0};
        copy.imageExtent = { tex.extent.width, tex.extent.height, 1 };

        vkCmdCopyBufferToImage(
            cmd,
            staging_buffer,
            res.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy);

        if (texture_creation_info.generate_mips)
        {
            generate_mipmaps(cmd, image, tex.extent.width, tex.extent.height, mip_levels);
        }

        if (texture_creation_info.current_layout != RBImageLayout::undefined)
        {
            ImageBarrierParams params_after_gen;
            params_after_gen.image = image;
            params_after_gen.src_usage = RBImageUsageType::TransferDst;
            params_after_gen.dst_usage = RBImageUsageType::SampledFragment;  // TODO

            transition_image(cmd, params_after_gen);
        }
    });

    vk::destroy_buffer(instance.device, staging_buffer, staging_memory);

    LogVkImageManager.Log<Verbose>("Allocated GPU texture: %s (layers=%u)",
        tex.name.c_str(), array_layers);

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
        params_before_copy.src_usage = RBImageUsageType::Undefined;
        params_before_copy.dst_usage = RBImageUsageType::TransferDst;
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
        params_after_copy.src_usage = RBImageUsageType::Undefined;
        params_after_copy.dst_usage = RBImageUsageType::SampledFragment;  // TODO

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
    desc.num_layers = 6;
    
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
        // =========================
        // BEFORE COPY
        // =========================
        ImageBarrierParams before{};
        before.image = image;

        before.src_usage = RBImageUsageType::Undefined;
        before.dst_usage = RBImageUsageType::TransferDst;

        transition_image(cmd, before);

        // =========================
        // COPY
        // =========================
        vkCmdCopyBufferToImage(
            cmd,
            staging_buffer,
            res.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(copies.size()),
            copies.data());

        ImageBarrierParams after{};
        after.image = image;
        after.src_usage = RBImageUsageType::TransferDst; 
        after.dst_usage = RBImageUsageType::SampledFragment;

        transition_image(cmd, after);
    });

    vk::destroy_buffer(instance.device, staging_buffer, staging_memory);

    LogVkImageManager.Log<Verbose>(
        "Allocated cubemap: %s (%u mips)",
        cubemap.name.to_string().c_str(),
        mip_levels);

    return image;
}

void vk::ImageManager::perform_image_copy(RBCommandList cmd, const CopyImageParams& params)
{
    const auto& src_resource = get_image_resource(params.source);
    const auto& dst_resource = get_image_resource(params.dest);

    const auto current_src_state = src_resource.get_state(params.src_layer, params.src_mip);
    const auto current_dst_state = dst_resource.get_state(params.dst_layer, params.dst_mip);
    
    const auto current_src_layout = current_src_state.layout;
    const auto current_dst_layout = current_dst_state.layout;
    
    LogVkImageManager.Log<Verbose>("Copying image '%s' (%p) -> '%s' (%p). Source layout: %s, Dest layout: %s",
        src_resource.debug_name.to_string().c_str(), src_resource.image,
        dst_resource.debug_name.to_string().c_str(), dst_resource.image,
        vk::enum_to_string(current_src_layout).data(), vk::enum_to_string(current_dst_layout).data());
    
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    if (is_depth_format(src_resource.format))
    {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

        if (has_stencil(dst_resource.format))
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    

    
    VkImageCopy region{};

    region.srcSubresource.aspectMask = aspect;
    region.srcSubresource.mipLevel = params.src_mip;
    region.srcSubresource.baseArrayLayer = params.src_layer;
    region.srcSubresource.layerCount = params.num_layers;

    region.srcOffset = {0, 0, 0};

    region.dstSubresource.aspectMask = aspect;
    region.dstSubresource.mipLevel = params.dst_mip;
    region.dstSubresource.baseArrayLayer = params.dst_layer;
    region.dstSubresource.layerCount = params.num_layers;

    region.dstOffset = {0, 0, 0};
    
    region.extent = {src_resource.extent.width, src_resource.extent.height, 1};
    
    vkCmdCopyImage(cmd.as<VkCommandBuffer>(), 
        src_resource.image, current_src_layout, 
        dst_resource.image, current_dst_layout,
        1,
        &region);
}

void vk::ImageManager::transition_image(
    RBCommandList cmd,
    const ImageBarrierParams& params) const
{
    
    auto& img = get_image_resource(params.image);
    
    
    if (params.debug_pass_name == "GeometryTranslucent" && img.debug_name == "RG_g_depth[0][0]")
    {
        __nop();
    }
    
    VkImage vk_img = img.image;

    uint32_t layer_count = params.layer_count ? params.layer_count : img.num_layers;
    uint32_t mip_count   = params.mip_count   ? params.mip_count   : img.mip_levels;

    ImageSubresourceState sub = img.get_state(params.base_layer, params.base_mip);

    // Source side of the barrier: use the recorded actual stage/access of
    // the previous transition. This is critical when the previous access was
    // in a different pass type than the current one — for example a compute
    // pass writing storage followed by a graphics pass sampling. Computing
    // src from to_vk_state(sub.usage, params.pass_type) would derive the
    // src stage from the CURRENT pass type, which is wrong: we need to
    // synchronize against the writer's actual stage. The sub.stage/sub.access
    // recorded by the prior transition is the truth.
    //
    // For dst we use the current pass type — that's the stage we're about
    // to perform the operation in.
    ImageState dst = to_vk_state(params.dst_usage, params.pass_type);

    VkImageLayout old_layout = sub.layout;
    VkImageLayout new_layout = dst.layout;

    // ----------------------------------------------------------------
    // Decide whether we can elide the barrier entirely.
    //
    // Old condition (BUGGY) was: skip if old_layout == new_layout &&
    // sub.usage == params.dst_usage. That mishandles three cases:
    //
    //   1) StorageImage -> StorageImage between two compute passes
    //      (RAW/WAW/WAR with no layout change). MUST emit a barrier
    //      with srcStage=COMPUTE, srcAccess=SHADER_STORAGE_WRITE,
    //      dstStage=COMPUTE, dstAccess=SHADER_STORAGE_READ|WRITE.
    //
    //   2) TransferDst -> TransferDst (two consecutive copies into
    //      the same image — see render_graph copy passes for ping-pong
    //      history textures). MUST emit a transfer-write WAW barrier.
    //
    //   3) ColorAttachment -> ColorAttachment between two distinct
    //      render passes that both write the same image. MUST emit
    //      a WAW barrier.
    //
    // We elide the barrier only if BOTH layouts match AND usage is a
    // pure-read kind (Sampled / SampledFragment / SampledVertex /
    // TransferSrc) — read-after-read on the same usage truly is a no-op.
    // ----------------------------------------------------------------
    auto is_pure_read = [](RBImageUsageType u)
    {
        switch (u)
        {
        case RBImageUsageType::Sampled:
        case RBImageUsageType::SampledFragment:
        case RBImageUsageType::SampledVertex:
        case RBImageUsageType::TransferSrc:
        case RBImageUsageType::DepthStencilReadOnly:
            return true;
        default:
            return false;
        }
    };

    if (old_layout == new_layout &&
        sub.usage == params.dst_usage &&
        is_pure_read(sub.usage))
    {
        return;
    }

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;

    // ----------------------------------------------------------------
    // srcAccess / srcStage selection.
    //
    // The previous code used VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT for
    // sub.usage == Undefined, with srcAccess=0. That is correct for
    // textures the engine just created, but it is INCORRECT for
    // swapchain images that were just acquired via vkAcquireNextImageKHR.
    // The acquire operation is conceptually a *read* of the swapchain
    // image at COLOR_ATTACHMENT_OUTPUT (that's what the wait-semaphore
    // chain is set up against), and Vulkan validation explicitly flags
    // the WRITE_AFTER_READ hazard if the layout transition's srcStage is
    // TOP_OF_PIPE — TOP_OF_PIPE doesn't synchronize with the acquire's
    // implicit read.
    //
    // Fix: when the image is a swapchain image AND prev usage is
    // Undefined or Present, source the dependency from
    // COLOR_ATTACHMENT_OUTPUT_BIT. The semaphore wait will resolve to
    // the same stage on the GPU, so this is free.
    // ----------------------------------------------------------------
    const bool is_swapchain = img.is_swapchain_image;

    if (sub.usage == RBImageUsageType::Undefined)
    {
        // No prior writer: nothing to synchronize against.
        barrier.srcAccessMask = 0;
    }
    else
    {
        // Source access bits from the previous transition's actual access mask.
        // For an Undefined->X transition we keep srcAccess=0 (nothing to wait
        // for). For everything else we MUST wait for the prior writer's
        // memory writes to flush — that means using whatever access mask
        // was recorded when the resource was last transitioned.
        barrier.srcAccessMask = sub.access;
    }

    barrier.dstAccessMask = dst.access;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vk_img;

    // aspect mask
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

    barrier.subresourceRange.baseMipLevel   = params.base_mip;
    barrier.subresourceRange.levelCount     = mip_count;
    barrier.subresourceRange.baseArrayLayer = params.base_layer;
    barrier.subresourceRange.layerCount     = layer_count;

    VkPipelineStageFlags src_stage;
    if (is_swapchain &&
        (sub.usage == RBImageUsageType::Undefined ||
         sub.usage == RBImageUsageType::Present))
    {
        // Synchronize the layout transition with vkAcquireNextImageKHR's
        // implicit read at COLOR_ATTACHMENT_OUTPUT.
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (sub.usage == RBImageUsageType::Undefined)
    {
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    else
    {
        // Source stage from the recorded actual stage of the previous
        // transition — NOT recomputed from the current pass type.
        src_stage = sub.stage;
    }

    VkPipelineStageFlags dst_stage = dst.stage;

    vkCmdPipelineBarrier(
        cmd.as<VkCommandBuffer>(),
        src_stage,
        dst_stage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    sub.layout = new_layout;
    sub.usage = params.dst_usage;
    sub.stage = dst_stage;
    sub.access = dst.access;
    img.set_state(sub, params.base_layer, layer_count, params.base_mip, mip_count);
}

VkImageLayout vk::ImageManager::get_image_layout(RBImageHandle image, uint32_t layer, uint32_t mip)
{
    auto& image_res = get_image_resource(image);
    return image_res.get_state(layer, mip).layout;
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
    
        ImageSubresourceState state;
        state.layout = barrier.newLayout;
        state.stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        state.access = barrier.dstAccessMask;
        res.set_state(state, 0, 1, i, 1);
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

    struct FormatInfo
    {
        TextureFormat out_format;
        uint32_t channels;
        uint32_t bytes_per_channel;
        enum class ComponentType { Float16, Float32, Unorm8 } component_type;
    };

    FormatInfo info{};

    switch (format)
    {
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            info = { TextureFormat::RGBA16F, 4, 2, FormatInfo::ComponentType::Float16 };
            break;

        case VK_FORMAT_R32G32B32A32_SFLOAT:
            info = { TextureFormat::RGBA32F, 4, 4, FormatInfo::ComponentType::Float32 };
            break;

        case VK_FORMAT_R32_SFLOAT:
            info = { TextureFormat::R32F, 1, 4, FormatInfo::ComponentType::Float32 };
            break;

        case VK_FORMAT_R16G16_SFLOAT:
            info = { TextureFormat::RG16F, 2, 2, FormatInfo::ComponentType::Float16 };
            break;

        case VK_FORMAT_R8G8B8A8_UNORM:
            info = { TextureFormat::RGBA8_UNORM, 4, 1, FormatInfo::ComponentType::Unorm8 };
            break;

        default:
            checkf(false, "Unsupported format for image readback: %i", int(format));
            return {};
    }

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
            Extent me = ImageReadback::mip_extent(base_extent, mip);
            size_t pixel_count = size_t(me.width) * me.height;
            size_t byte_size   = pixel_count * info.channels * info.bytes_per_channel;

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
    result.mips = mip_count;
    result.format = info.out_format;
    result.channels = info.channels;

    result.data.resize(layers);

    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        result.data[layer].resize(mip_count);

        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            const Extent& me = mip_infos[layer][mip].extent;
            size_t pixel_count = size_t(me.width) * me.height;
            result.data[layer][mip].resize(pixel_count * info.channels);
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
            const auto& mi = mip_infos[layer][mip];

            VkBufferImageCopy region{};
            region.bufferOffset = mi.offset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;

            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;

            region.imageExtent = {
                mi.extent.width,
                mi.extent.height,
                1
            };

            regions.push_back(region);
        }
    }

    immediate_command_pool.submit([&](VkCommandBuffer cmd)
    {
        ImageBarrierParams params;
        params.image = img;
        params.src_usage = RBImageUsageType::Undefined;
        params.dst_usage = RBImageUsageType::TransferSrc;
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
            const auto& mi = mip_infos[layer][mip];

            const uint8_t* src_bytes =
                reinterpret_cast<const uint8_t*>(mapped) + mi.offset;

            float* dst = result.data[layer][mip].data();

            const size_t pixel_count = size_t(mi.extent.width) * mi.extent.height;
            const size_t value_count = pixel_count * info.channels;

            switch (info.component_type)
            {
                case FormatInfo::ComponentType::Float32:
                {
                    memcpy(dst, src_bytes, value_count * sizeof(float));
                    break;
                }
                case FormatInfo::ComponentType::Float16:
                {
                    const uint16_t* src =
                        reinterpret_cast<const uint16_t*>(src_bytes);
                    for (size_t i = 0; i < value_count; ++i)
                        dst[i] = math::half_to_float(src[i]);
                    break;
                }
                case FormatInfo::ComponentType::Unorm8:
                {
                    const uint8_t* src = src_bytes;
                    constexpr float inv_255 = 1.0f / 255.0f;
                    for (size_t i = 0; i < value_count; ++i)
                        dst[i] = float(src[i]) * inv_255;
                    break;
                }
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