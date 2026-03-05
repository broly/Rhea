export module vk:pipeline_graphics;

import <vulkan/vulkan_core.h>;

import :context;
import :shader;
import :instance;
import :swapchain_control;
import :buffer_mgr;
import render;
import <unordered_map>;
import <optional>;
import <cassert>;
import :render_resource;
import :render_resource_instance;
import :reflection;
import :pipeline;


class VkPipelineObject_Graphics : public VkPipelineObject
{
public:
    VkPipelineObject_Graphics(
        vk::Instance& in_instance,
        vk::SwapchainControl& in_swapchain,
        vk::BufferManager& in_buffer_manager,
        const GraphicsPipelineDesc& desc);
    ~VkPipelineObject_Graphics();

    RBPipelineLayout get_layout() const override
    {
        return pipeline_desc.layout.pipeline_layout;
    }

    virtual VkPipeline create_pipeline(VkRenderPass render_pass) override;

    
    GraphicsPipelineDesc pipeline_desc;
    
    
};
