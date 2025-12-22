#pragma once
#include <vulkan/vulkan_core.h>

#include "vk_context.h"
#include "vk_shader.h"

struct GraphicsPipelineDesc;

class VkPipelineObject
{
public:
    VkPipelineObject(
        vk::InstanceContext& in_instance_context, 
        vk::SwapchainContext& in_swapchain_context,
        const GraphicsPipelineDesc& desc,
        class VkRenderBackend& backend);
    ~VkPipelineObject();

    VkPipeline get_handle() const { return pipeline_; }
    
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
