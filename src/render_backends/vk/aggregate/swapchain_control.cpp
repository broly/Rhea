module vk:swapchain_control;

import :helpers;
import :log;
import :render_backend;
import <iostream>;
import <vulkan/vulkan_core.h>;

import render;

#include "render_backends/vk/vk_macro.h"

void vk::SwapchainControl::init()
{
    
    vk::SwapchainSupport support = vk::query_swapchain_support(
        instance.physical_device, instance.surface);

    // --- Surface format ---
    surface_format = vk::choose_surface_format(support.formats);

    VkPresentModeKHR present_mode = vk::choose_present_mode(support.present_modes);

    extent = vk::choose_extent(support.caps, instance.window);

    // --- Image count ---
    uint32_t image_count = support.caps.minImageCount + 1;
    if (support.caps.maxImageCount > 0 &&
        image_count > support.caps.maxImageCount)
    {
        image_count = support.caps.maxImageCount;
    }

    // --- Swapchain create info ---
    VkSwapchainCreateInfoKHR sci{
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
    };
    sci.surface = instance.surface;
    sci.minImageCount = image_count;
    sci.imageFormat = surface_format.format;
    sci.imageColorSpace = surface_format.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (instance.queues.graphics !=
        instance.queues.present)
    {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = instance.queues_indices;
    }
    else
    {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    sci.preTransform   = support.caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode    = present_mode;
    sci.clipped        = VK_TRUE;
    sci.oldSwapchain   = VK_NULL_HANDLE;

    VK_CHECK(
        vkCreateSwapchainKHR(instance.device, &sci, nullptr, &swapchain)
    );

    // --- Get swapchain images ---
    uint32_t sc_image_count = 0;
    vkGetSwapchainImagesKHR(instance.device, swapchain, 
        &sc_image_count, nullptr);

    std::vector<VkImage> swapchain_images(sc_image_count);
    vkGetSwapchainImagesKHR(instance.device, swapchain, 
        &sc_image_count, swapchain_images.data());
    
    swapchain_image_handles.clear();
    swapchain_image_handles.resize(sc_image_count);
    

    for (uint32_t i = 0; i < sc_image_count; ++i)
    {
        VkImageView view;

        VkImageViewCreateInfo ivci{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };
        ivci.image = swapchain_images[i];
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
        res.image  = swapchain_images[i];                 // VkImage
        res.view   = view;                                // VkImageView
        res.format = surface_format.format;
        res.width  = extent.width;
        res.height = extent.height;

        uint32_t id = static_cast<uint32_t>(image_resources.size());
        image_resources.push_back(res);

        swapchain_image_handles[i] = RBImageHandle{ id };
    }
}

RBSwapchainExtent vk::SwapchainControl::get_extent() const
{
    return RBSwapchainExtent{extent.width, extent.height};
}

void vk::SwapchainControl::CRUTCH_transition_image(const RBCommandList& cmd, RBImageHandle image,
    TextureFormat format, VkImageLayout old_layout, VkImageLayout new_layout)
{
    auto& img = image_resources[image.id];

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.image = img.image;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    else
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;

    if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
        new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    
    
    
    if (vk::is_depth_format(vk::to_vk_format(format)))
    {
        barrier.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    else
    {
        barrier.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
    }

    vkCmdPipelineBarrier(
        cmd.as<VkCommandBuffer>(),
        src_stage,
        dst_stage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

RBImageHandle vk::SwapchainControl::create_image(RBImageDesc desc)
{    
    uint32_t width  = desc.width;
    uint32_t height = desc.height;

    if (width == 0 || height == 0)
    {
        width  = extent.width;
        height = extent.height;
    }

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

RBImageView vk::SwapchainControl::get_image_view(RBImageHandle handle) const
{
    return image_resources[handle.id].view;
}

RBImageView vk::SwapchainControl::get_image_view() const
{
    return image_views[swapchain_image_index];
}

RBImageView vk::SwapchainControl::resolve_image_view(const RGTexture& tex, uint32_t frame)
{
    if (tex.desc.external)
    {
        // swapchain
        return image_views[swapchain_image_index];
    }

    return image_resources[tex.image.value().id].view;
}

RBImageHandle vk::SwapchainControl::get_image() const
{
    return swapchain_image_handles[swapchain_image_index];
}

void vk::SwapchainControl::update_depth_descriptior(const RBDescriptorSet& rb_handle, RBImageHandle value)
{
    const auto& res = image_resources[value.id];
    

    VkDescriptorImageInfo image_info{};
    image_info.imageView = res.view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.sampler = texture_manager.get_default_sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = rb_handle.as<VkDescriptorSet>();
    write.dstBinding = 0; // u_depth
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image_info;

    vkUpdateDescriptorSets(instance.device, 1, &write, 0, nullptr);
}

VkFormat vk::SwapchainControl::get_image_format(RBImageHandle handle) const
{
    return image_resources[handle.id].format;
}

bool vk::SwapchainControl::acquire_next_image(uint32_t frame_handle)
{
    LogRB.Log("acquire_next_image");
    
    auto& frame = frames[frame_handle];

    VkResult res = vkAcquireNextImageKHR(
        instance.device,
        swapchain,
        UINT64_MAX,
        frame.image_available, // semaphore
        VK_NULL_HANDLE,
        &swapchain_image_index
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // recreate_swapchain();
        return false;
    }

    VK_CHECK(res);
    return true;
}

void vk::SwapchainControl::submit_frame(RBFrameHandle frame_handle, const RBCommandList& cmd_list)
{
    LogRB.Log("submit_frame");
    auto& frame = frames[frame_handle];

    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();

    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphore render_finished =
    render_finished_per_image[swapchain_image_index];

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished;

    VK_CHECK(vkQueueSubmit(
        instance.graphics_queue,
        1,
        &submit, 
        frame.in_flight
    ));

    
    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &swapchain_image_index;

    auto res = vkQueuePresentKHR(
        instance.present_queue, &present);

    if (res == VK_ERROR_OUT_OF_DATE_KHR ||
        res == VK_SUBOPTIMAL_KHR ||
        framebuffer_resized)
    {
        framebuffer_resized = false;
        // backend.recreate_swapchain();
    }
    else
    {
        VK_CHECK(res);
    }
}

void vk::SwapchainControl::advance_frame()
{
    LogRB.Log("advance_frame");
    current_frame = (current_frame + 1) % vk::MAX_FRAMES_IN_FLIGHT;
}

void vk::SwapchainControl::cleanup()
{
    
    // --- Image views ---
    for (auto iv : image_views)
    {
        if (iv != VK_NULL_HANDLE)
        {
            vkDestroyImageView(instance.device, iv, nullptr);
        }
    }
    image_views.clear();

    // --- Depth resources ---
    if (depth_image_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(instance.device, depth_image_view, nullptr);
    }

    if (depth_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(instance.device, depth_image, nullptr);
    }

    if (depth_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(instance.device, depth_memory, nullptr);
    }

    depth_image = VK_NULL_HANDLE;
    depth_image_view = VK_NULL_HANDLE;
    depth_memory = VK_NULL_HANDLE;

    // --- Swapchain ---
    if (swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(instance.device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

}

void vk::SwapchainControl::create_sync_objects()
{
    VkSemaphoreCreateInfo sem_ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    VkFenceCreateInfo fence_ci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // --- frame sync ---
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VK_CHECK(vkCreateSemaphore(
            instance.device, &sem_ci, nullptr,
            &frames[i].image_available));

        VK_CHECK(vkCreateFence(
            instance.device, &fence_ci, nullptr,
            &frames[i].in_flight));
    }

    // --- per swapchain image sync ---
    render_finished_per_image.resize(swapchain_image_handles.size());

    for (size_t i = 0; i < render_finished_per_image.size(); ++i)
    {
        VK_CHECK(vkCreateSemaphore(
            instance.device, &sem_ci, nullptr,
            &render_finished_per_image[i]));
    }
}


void vk::SwapchainControl::wait_for_frame(RBFrameHandle frame_handle)
{
    LogRB.Log("wait_for_frame");
    
    auto& f = frames[frame_handle];

    vkWaitForFences(
        instance.device,
        1,
        &f.in_flight,
        VK_TRUE,
        UINT64_MAX
    );
}

void vk::SwapchainControl::reset_frame_fence(uint32_t frame)
{
    auto& f = frames[frame];

    vkResetFences(instance.device, 1, &f.in_flight);
}
