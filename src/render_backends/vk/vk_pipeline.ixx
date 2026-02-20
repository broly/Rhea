export module vk:pipeline;

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


class VkPipelineObject : public PipelineObject
{
public:
    VkPipelineObject(
        vk::Instance& in_instance,
        vk::SwapchainControl& in_swapchain,
        vk::BufferManager& in_buffer_manager,
        const GraphicsPipelineDesc& desc);
    ~VkPipelineObject();

    void prepare();
    
    RBPipelineLayout get_layout() const override
    {
        return pipeline_desc->layout.pipeline_layout;
    }

    RBPipelineHandle get_pipeline_handle() const
    {
        assert(vk_pipeline != VK_NULL_HANDLE); 
        return vk_pipeline;
    }
    
    VkPipeline get_or_create_pipeline(VkRenderPass render_pass);

    
    std::optional<GraphicsPipelineDesc> pipeline_desc;
    
    void reflect_shader(const VkShader& shader, ShaderStage stage);

    const std::map<ShaderStage, PipelineReflection>& get_reflection() const
    {
        return pipeline_reflection;
    }

public:
    vk::Instance& instance;
    vk::SwapchainControl& swapchain;
    vk::BufferManager& buffer_manager;

    VkPipeline vk_pipeline = VK_NULL_HANDLE;
    
    // VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    
    std::vector<VkShader> shaders;
    
    std::map<ShaderStage, PipelineReflection> pipeline_reflection;
    
    
    
    
};
