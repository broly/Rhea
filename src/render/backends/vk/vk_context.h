#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

struct FrameSync
{
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;
};


constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
constexpr uint32_t MAX_QUEUES = 2;


struct QueueFamilies {
    uint32_t graphics = UINT32_MAX;
    uint32_t present  = UINT32_MAX;

    bool complete() const {
        return graphics != UINT32_MAX &&
               present  != UINT32_MAX;
    }
};

struct VkContext
{
    VkDevice device;
    VkSwapchainKHR swapchain;
    VkQueue graphics_queue;
    VkQueue present_queue;
    
    uint32_t queues_indices[MAX_QUEUES];
    
    FrameSync frames[MAX_FRAMES_IN_FLIGHT] = {FrameSync{}};
    std::vector<VkSemaphore> render_finished_per_image;
    std::vector<VkFence> images_in_flight;
    uint32_t current_frame = 0;
    std::vector<VkCommandBuffer> command_buffers;
    VkRenderPass render_pass;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkImageView> swapchain_image_views;
    VkExtent2D extent;
    uint32_t images_count;
    VkSurfaceKHR surface;
    VkSurfaceFormatKHR surface_format;
    VkPhysicalDevice physical_device;
    VkInstance instance;
    QueueFamilies queues;
    VkCommandPool command_pool;
};
