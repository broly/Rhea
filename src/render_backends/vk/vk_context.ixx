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
    
    struct ImageSubresourceState
    {
        RBImageUsageType usage = RBImageUsageType::Undefined;
        RenderPassType pass_type = RenderPassType::graphics;

        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkAccessFlags access = 0;
    };
    
    struct ImageResource
    {
        Name debug_name = "";
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        
        VkImageView cubemap_view = VK_NULL_HANDLE;
        
        std::vector<VkImageView> views = {};
        bool destroyed = false;
        
        void init_states()
        {
            subresources.resize(num_layers);
            for (uint32_t l = 0; l < num_layers; ++l)
            {
                subresources[l].resize(mip_levels);

                for (uint32_t m = 0; m < mip_levels; ++m)
                {
                    subresources[l][m] = {
                        .usage = RBImageUsageType::Undefined,
                        .pass_type = RenderPassType::graphics,
                        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        .access = 0
                    };
                }
            }
        }
        
        uint32_t get_array_index(uint32_t layer_index = 0, uint32_t mip_index = 0) const
        {
            checkf(!destroyed, "Image has been destroyed");
            return mip_index * mip_levels + layer_index;
        }
        
        void set_img_view(VkImageView view, uint32_t layer_index = 0, uint32_t mip_index = 0)
        {
            checkf(!destroyed, "Image has been destroyed");
            const uint32_t array_index = get_array_index(layer_index, mip_index);
            checkf(layer_index < num_layers, "Image view layer out of bounds");
            checkf(mip_index < mip_levels, "Image view mip index out of bounds");
            if (array_index >= views.size())
                views.resize(array_index + 1, VK_NULL_HANDLE);
            views[array_index] = view;
        }
        
        
        bool is_swapchain_image = false;
        void set_cubemap_img_view(VkImageView view)
        {
            checkf(!destroyed, "Image has been destroyed");
            cubemap_view = view;
        }
        
        bool has_cubemap() const
        {
            checkf(!destroyed, "Image has been destroyed");
            return cubemap_view != VK_NULL_HANDLE;
        }
        
        bool has_view(uint32_t layer_index, uint32_t mip_index = 0) const
        {
            checkf(!destroyed, "Image has been destroyed");
            const uint32_t array_index = get_array_index(layer_index, mip_index);
            return array_index < views.size() && views[array_index] != VK_NULL_HANDLE;
        }
        
        bool is_valid_view(uint32_t layer_index, uint32_t mip_index = 0) const
        {
            checkf(!destroyed, "Image has been destroyed");
            return layer_index < num_layers && mip_index < mip_levels;
        }
        
        VkImageView get_img_view(uint32_t layer_index = 0, uint32_t mip_index = 0) const
        {
            checkf(!destroyed, "Image has been destroyed");
            const uint32_t array_index = get_array_index(layer_index, mip_index);
            checkf(layer_index < num_layers, "Image view layer out of bounds");
            checkf(mip_index < mip_levels, "Image view mip level out of bounds");
            auto result = views[array_index];
            checkf(result != VK_NULL_HANDLE, "NULL image handle detected");
            return result;
        }
        
        VkImageView get_cubemap_view() const
        {
            checkf(!destroyed, "Image has been destroyed");
            return cubemap_view;
        }
        
        const ImageSubresourceState& get_state(uint32_t layer, uint32_t mip) const
        {
            checkf(!destroyed, "Image has been destroyed");
            checkf(layer < num_layers, "Layer out of bounds");
            checkf(mip < mip_levels, "Mip out of bounds");

            return subresources[layer][mip];
        }
        
        
        ImageSubresourceState& get_mutable_state(uint32_t layer, uint32_t mip) const
        {
            checkf(!destroyed, "Image has been destroyed");
            checkf(layer < num_layers, "Layer out of bounds");
            checkf(mip < mip_levels, "Mip out of bounds");

            return subresources[layer][mip];
        }
        
        void set_state(const ImageSubresourceState& new_state,
               uint32_t base_layer = 0, uint32_t layer_count = 1,
               uint32_t base_mip = 0, uint32_t mip_count = 1) const
        {
            if (layer_count == 0)
                layer_count = num_layers;

            if (mip_count == 0)
                mip_count = mip_levels;

            for (uint32_t l = 0; l < layer_count; ++l)
            {
                for (uint32_t m = 0; m < mip_count; ++m)
                {
                    subresources[base_layer + l][base_mip + m] = new_state;
                }
            }
        }
        
        void mark_destroyed()
        {
            destroyed = true;
        }

        Extent extent = {};
        VkFormat format = VK_FORMAT_UNDEFINED;
        
        uint32_t mip_levels = 1; 
        uint32_t num_layers = 1;
        Mask<RenderTextureUsage::Type> usage = RenderTextureUsage::None;
        
        mutable std::vector<std::vector<ImageSubresourceState>> subresources;
    };
}
