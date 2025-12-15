#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

struct VkContext
{
    VkDevice device;
    VkSwapchainKHR swapchain;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
    std::vector<VkCommandBuffer> command_buffers;
    VkRenderPass render_pass;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkImageView> swapchain_image_views;
    VkExtent2D extent;
};
