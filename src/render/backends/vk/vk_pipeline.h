#pragma once
#include <vulkan/vulkan_core.h>

#include "vk_context.h"
#include "vk_shader.h"
#include "render/pipeline_object.h"

struct GraphicsPipelineDesc;

class VkPipelineObject : public PipelineObject
{
public:
    VkPipelineObject(
        vk::InstanceContext& in_instance_context, 
        vk::SwapchainContext& in_swapchain_context,
        const GraphicsPipelineDesc& desc,
        class VkRenderBackend& backend);
    ~VkPipelineObject();

    RBPipelineHandle get_pipeline_handle() const override { return pipeline_; }
    
    VkPipelineLayout get_pipeline_layout() const
    {
        return pipeline_layout;
    }
    VkPipeline get_or_create_pipeline(VkRenderBackend& backend, vk::SwapchainContext& swapchain, VkRenderPass render_pass);

private:
    vk::InstanceContext& instance_context;
    vk::SwapchainContext& swapchain_context;
    

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    
    std::optional<VkShader> vert;
    std::optional<VkShader> frag;
    
    // std::vector<VkShader> shaders;
};
