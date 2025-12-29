export module vk:pipeline;

import <vulkan/vulkan_core.h>;

import :context;
import :shader;
import :instance;
import :swapchain_control;
import :buffer_mgr;
import render;
import <optional>;
import <cassert>;


class VkPipelineObject : public PipelineObject
{
public:
    VkPipelineObject(
        const GraphicsPipelineDesc& desc,
        vk::Instance& in_instance,
        vk::SwapchainControl& in_swapchain,
        vk::BufferManager& in_buffer_manager);
    ~VkPipelineObject();

    RBPipelineHandle get_pipeline_handle() const override
    {
        assert(pipeline_ != VK_NULL_HANDLE); 
        return pipeline_;
    }
    
    VkPipelineLayout get_pipeline_layout() const
    {
        assert(pipeline_ != VK_NULL_HANDLE); 
        return pipeline_layout;
    }
    VkPipeline get_or_create_pipeline(VkRenderPass render_pass);
    
    GraphicsPipelineDesc pipeline_desc;

private:
    vk::Instance& instance;
    vk::SwapchainControl& swapchain;
    vk::BufferManager& buffer_manager;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    
    std::optional<VkShader> vert;
    std::optional<VkShader> frag;
    
};
