module vk:swapchain_control;

import :helpers;
import :log;
import :render_backend;
import <iostream>;
import <vulkan/vulkan_core.h>;

import render;

#include "profiling/profile.h"
#include "render_backends/vk/vk_macro.h"

void vk::SwapchainControl::create()
{
    init(VK_NULL_HANDLE);
    
    create_sync_objects();
}

void vk::SwapchainControl::init(VkSwapchainKHR old_swapchain)
{
    vk::SwapchainSupport support =
        vk::query_swapchain_support(
            instance.physical_device,
            instance.surface);

    surface_format =
        vk::choose_surface_format(support.formats);

    VkPresentModeKHR present_mode =
        vk::choose_present_mode(support.present_modes);

    vk_extent =
        vk::choose_extent(support.caps, instance.window);

    const Extent extent = {
        vk_extent.width,
        vk_extent.height
    };

    image_manager.set_default_extent(extent);

    uint32_t image_count =
        support.caps.minImageCount + 1;

    if (support.caps.maxImageCount > 0 &&
        image_count > support.caps.maxImageCount)
    {
        image_count = support.caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
    };

    sci.surface = instance.surface;
    sci.minImageCount = image_count;
    sci.imageFormat = surface_format.format;
    sci.imageColorSpace = surface_format.colorSpace;
    sci.imageExtent = vk_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.preTransform = support.caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = present_mode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = old_swapchain;

    if (instance.queues.graphics != instance.queues.present)
    {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = instance.queues_indices;
    }
    else
    {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(
        instance.device, &sci, nullptr, &swapchain));

    if (old_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(
            instance.device,
            old_swapchain,
            nullptr);
    }

    // --- Images ---
    uint32_t count = 0;
    vkGetSwapchainImagesKHR(instance.device, swapchain, &count, nullptr);

    std::vector<VkImage> images(count);
    vkGetSwapchainImagesKHR(instance.device, swapchain, &count, images.data());

    bool has_swapchain_images = swapchain_image_handles.size() > 0;
    if (!has_swapchain_images)
    {
        swapchain_image_handles.resize(count);
    }
    
    
    for (uint32_t i = 0; i < count; ++i)
    {
        auto old_image_handle = swapchain_image_handles[i];
        swapchain_image_handles[i] =
            image_manager.register_swapchain_image(
                vk_extent,
                surface_format,
                images[i],
                has_swapchain_images ? std::optional{old_image_handle} : std::nullopt);
    }
    
    // images_in_flight.resize(swapchain_image_handles.size(), VK_NULL_HANDLE);
    
    render_finished_per_image.resize(swapchain_image_handles.size());

    VkSemaphoreCreateInfo sem_ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    for (size_t i = 0; i < render_finished_per_image.size(); ++i)
    {
        VK_CHECK(vkCreateSemaphore(
            instance.device,
            &sem_ci,
            nullptr,
            &render_finished_per_image[i]));
    }
}

void vk::SwapchainControl::recreate_swapchain()
{
    LogVkSwapchain.Log("recreate_swapchain");

    int width = 0;
    int height = 0;

    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(
            instance.window,
            &width,
            &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(instance.device);

    VkSwapchainKHR old_swapchain = swapchain;
    
    // for (auto image : swapchain_image_handles)
    // {
    //     image_manager.unregister_swapchain_image(image);
    // }

    init(old_swapchain);

    current_swapchain_index = 0;
}



Extent vk::SwapchainControl::get_extent() const
{
    return Extent{vk_extent.width, vk_extent.height};
}

RBImageHandle vk::SwapchainControl::get_image(RBFrameHandle frame_handle) const
{
    return swapchain_image_handles[current_swapchain_index];
}

bool vk::SwapchainControl::acquire_next_image(uint32_t frame)
{
    auto& f = frames[frame];

    VkResult res = vkAcquireNextImageKHR(
        instance.device,
        swapchain,
        UINT64_MAX,
        f.image_available,
        VK_NULL_HANDLE,
        &current_swapchain_index);

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        recreate_swapchain();
        return false;
    }

    VK_CHECK(res);
    return true;
}


bool vk::SwapchainControl::submit_frame(
    RBFrameHandle frame,
    const RBCommandList& cmd_list)
{
    auto& f = frames[frame];

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &f.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = cmd_list.ptr<VkCommandBuffer>();
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_per_frame[frame];

    (vkQueueSubmit(instance.graphics_queue, 1, &submit, f.in_flight));

    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished_per_frame[frame];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &current_swapchain_index;

    VkResult res = vkQueuePresentKHR(instance.present_queue, &present);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebuffer_resized)
    {
        framebuffer_resized = false;
        recreate_swapchain();
        return false;
    }

    VK_CHECK(res);
    return true;
}


void vk::SwapchainControl::advance_frame()
{
    LogVkSwapchain.Log<VeryVerbose>("advance_frame");
    current_frame = (current_frame + 1) % vk::MAX_FRAMES_IN_FLIGHT;
}

void vk::SwapchainControl::cleanup()
{

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

    for (auto sem : render_finished_per_image)
    {
        if (sem != VK_NULL_HANDLE)
            vkDestroySemaphore(instance.device, sem, nullptr);
    }

    render_finished_per_image.clear();
}

void vk::SwapchainControl::create_sync_objects()
{
    VkSemaphoreCreateInfo sem_ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_ci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VK_CHECK(vkCreateSemaphore(instance.device, &sem_ci, nullptr, &frames[i].image_available));
        VK_CHECK(vkCreateSemaphore(instance.device, &sem_ci, nullptr, &render_finished_per_frame[i]));
        VK_CHECK(vkCreateFence(instance.device, &fence_ci, nullptr, &frames[i].in_flight));
    }
}

void vk::SwapchainControl::wait_for_frame(uint32_t frame)
{
    vkWaitForFences(instance.device, 1, &frames[frame].in_flight, VK_TRUE, UINT64_MAX);
}


void vk::SwapchainControl::reset_frame_fence(uint32_t frame)
{
    PROFILE(__FUNCTION__);
    auto& f = frames[frame];

    vkResetFences(instance.device, 1, &frames[frame].in_flight);
}
