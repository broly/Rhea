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
        
        VkImageView cubemap_view = VK_NULL_HANDLE;
        
        std::vector<VkImageView> views = {};
        
        uint32_t get_array_index(uint32_t layer_index = 0, uint32_t mip_index = 0) const
        {
            return mip_index * mip_levels + layer_index;
        }
        
        void set_img_view(VkImageView view, uint32_t layer_index = 0, uint32_t mip_index = 0)
        {
            const uint32_t array_index = get_array_index(layer_index, mip_index);
            checkf(layer_index < num_layers, "Image view layer out of bounds");
            checkf(mip_index < mip_levels, "Image view mip index out of bounds");
            if (layer_index >= views.size())
                views.resize(array_index + 1, VK_NULL_HANDLE);
            views[array_index] = view;
        }
        
        
        void set_cubemap_img_view(VkImageView view)
        {
            cubemap_view = view;
        }
        
        bool has_cubemap() const
        {
            return cubemap_view != VK_NULL_HANDLE;
        }
        
        bool has_view_index(uint32_t layer_index, uint32_t mip_index = 0) const
        {
            const uint32_t array_index = get_array_index(layer_index, mip_index);
            return array_index < views.size() && views[array_index] != VK_NULL_HANDLE;
        }
        
        VkImageView get_img_view(uint32_t layer_index = 0, uint32_t mip_index = 0) const
        {
            const uint32_t array_index = get_array_index(layer_index, mip_index);
            checkf(layer_index < num_layers, "Image view layer out of bounds");
            checkf(mip_index < mip_levels, "Image view mip level out of bounds");
            auto result = views[array_index];
            checkf(result != VK_NULL_HANDLE, "NULL image handle detected");
            return result;
        }
        
        VkImageView get_cubemap_view() const
        {
            return cubemap_view;
        }

        Extent extent = {};
        VkFormat format = VK_FORMAT_UNDEFINED;
        
        uint32_t mip_levels   = 1; 
        uint32_t num_layers = 1;
        Mask<RenderTextureUsage::Type> usage = RenderTextureUsage::None;
    };
}
