export module vk:context;

import <array>;
import <vector>;
import <vulkan/vulkan_core.h>;
import <map>;
import rhmath;
import enum_helpers;
import render;
import name;
#include "common/assertion_macros.h"

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
        Name debug_name = "";
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        
        std::vector<VkImageView> views = {};
        
        void set_img_view(VkImageView view, uint32_t array_index = 0)
        {
            checkf(array_index < array_layers, "Image view index out of bounds");
            if (array_index >= views.size())
                views.resize(array_index + 1, VK_NULL_HANDLE);
            views[array_index] = view;
        }
        
        bool has_view_index(uint32_t array_index) const
        {
            return array_index < views.size() && views[array_index] != VK_NULL_HANDLE;
        }
        
        VkImageView get_img_view(uint32_t array_index = 0) const
        {
            checkf(array_index < array_layers, "Image view index out of bounds");
            auto result = views[array_index];
            checkf(result != VK_NULL_HANDLE, "NULL image handle detected");
            return result;
        }

        Extent extent = {};
        VkFormat format = VK_FORMAT_UNDEFINED;
        
        uint32_t mip_levels   = 1; 
        uint32_t array_layers = 1;
        Mask<RenderTextureUsage::Type> usage = RenderTextureUsage::None;
    };
}
