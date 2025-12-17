#pragma once

#include <array>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace vk
{
    struct FrameContext {
        VkCommandBuffer cmd = VK_NULL_HANDLE;

        VkSemaphore image_available = VK_NULL_HANDLE;
        VkFence in_flight = VK_NULL_HANDLE;

        VkBuffer camera_buffer = VK_NULL_HANDLE;
        VkDeviceMemory camera_memory = VK_NULL_HANDLE;
        VkDescriptorSet camera_set = VK_NULL_HANDLE;
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

    
    struct InstanceContext
    {
        VkInstance instance;
        VkDevice device;
        VkPhysicalDevice physical_device;
        VkQueue graphics_queue;
        VkQueue present_queue;
        QueueFamilies queues;
    
        uint32_t queues_indices[MAX_QUEUES];
        VkSurfaceKHR surface;
    };
    
    struct SwapchainContext {
        VkSwapchainKHR swapchain;
        VkExtent2D extent;

        std::vector<VkImageView> image_views;

        VkRenderPass render_pass;
        std::vector<VkFramebuffer> framebuffers;

    
        VkImage depth_image = VK_NULL_HANDLE;
        VkDeviceMemory depth_memory = VK_NULL_HANDLE;
        VkImageView depth_image_view = VK_NULL_HANDLE;
        VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
        
        
        VkSurfaceFormatKHR surface_format;
    };

    struct DescriptorContext
    {
        VkDescriptorSetLayout camera_set_layout;
        VkDescriptorPool pool;
    };
    struct PipelineContext
    {
        VkPipeline pipeline;
        VkPipelineLayout layout;
    };
    
    struct FrameScheduleContext
    {
        std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frames;
        uint32_t current_frame;
    };
    
    
    struct CommandContext
    {
        VkCommandPool command_pool;
    };
    
    struct ImageContext
    {
        // swapchain image related
        VkImage image;
        VkImageView image_view;

        VkFramebuffer framebuffer;

        // synchronization
        VkSemaphore render_finished;    // signal -> present
        VkFence in_flight;              // image is used by which frame
    };


    struct Context
    {
        
        // descriptors
        std::vector<VkDescriptorSet> camera_descriptor_sets;
    
    };
}
