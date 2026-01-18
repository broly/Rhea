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
    
    image_manager.set_default_extent(extent.width, extent.height);

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
        auto handle = image_manager.create_image_view(extent, surface_format, swapchain_images[i]);
        swapchain_image_handles[i] = handle;
    }
}

void vk::SwapchainControl::recreate_swapchain()
{
    LogRB.Log("recreate_swapchain");

    int width = 0;
    int height = 0;

    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(instance.window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(instance.device);

    cleanup();

    init();

    for (auto sem : render_finished_per_image)
    {
        vkDestroySemaphore(instance.device, sem, nullptr);
    }
    render_finished_per_image.clear();

    create_sync_objects();

    current_swapchain_index = 0;
}



RBSwapchainExtent vk::SwapchainControl::get_extent() const
{
    return RBSwapchainExtent{extent.width, extent.height};
}

RBImageView vk::SwapchainControl::get_image_view() const
{
    return image_views[current_swapchain_index];
}

RBImageHandle vk::SwapchainControl::get_image() const
{
    return swapchain_image_handles[current_swapchain_index];
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
        &current_swapchain_index
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        recreate_swapchain();
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
    render_finished_per_image[current_swapchain_index];

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
    present.pImageIndices = &current_swapchain_index;

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
