#pragma once
#include <vulkan/vulkan_core.h>

#include "vk_context.h"
#include "vk_shader.h"

class VkPipelineObject
{
public:
    VkPipelineObject(
        vk::InstanceContext& in_instance_context, 
        vk::SwapchainContext& in_swapchain_context,
        const VkDescriptorSetLayout camera_set_layout);
    ~VkPipelineObject();

    VkPipeline get_pipeline() const { return pipeline_; }
    
    VkPipelineLayout get_pipeline_layout() const
    {
        return pipeline_layout;
    }

private:
    vk::InstanceContext& instance_context;
    vk::SwapchainContext& swapchain_context;

    VkPipeline pipeline_;
    
    VkPipelineLayout pipeline_layout;
    
    std::vector<VkShader> shaders;
};
