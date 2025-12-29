export module vk:context;

import <array>;
import <vector>;
import <vulkan/vulkan_core.h>;
import <map>;

import render;

namespace vk
{
    


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

    
    struct PipelineContext
    {
        VkPipeline pipeline;
        VkPipelineLayout layout;
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
    
    struct ImageResource
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;

        // one view per image
        VkImageView view = VK_NULL_HANDLE;

        uint32_t width = 0;
        uint32_t height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };
}
