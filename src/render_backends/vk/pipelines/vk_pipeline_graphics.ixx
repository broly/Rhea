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
        RBPipelineLayout pipeline_layout,
        const PipelineCreateDesc_Graphics& desc);

    virtual VkPipeline create_pipeline(VkRenderPass render_pass) override;
    
    virtual VkPipelineBindPoint get_bind_point() override
    {
        return VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
    
    PipelineCreateDesc_Graphics pipeline_desc;
};
