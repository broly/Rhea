export module vk:pipeline;
import :instance;
import :swapchain_control;
import :buffer_mgr;
import <vulkan/vulkan_core.h>;
import :shader;
import :reflection;
import render;

#include "common/assertion_macros.h"

export class VkPipelineObject : public PipelineObject
{
public:
    
    VkPipelineObject(
        vk::Instance& in_instance,
        vk::SwapchainControl& in_swapchain,
        vk::BufferManager& in_buffer_manager)
            : instance(in_instance)
            , swapchain(in_swapchain)
            , buffer_manager(in_buffer_manager)
    {
        
    }
    
    
    VkPipeline get_or_create_pipeline(VkRenderPass render_pass)
    {
        if (vk_pipeline != nullptr)
            return vk_pipeline;
    
        return create_pipeline(render_pass);
    }
    
    
    
    virtual VkPipeline create_pipeline(VkRenderPass render_pass) pure;
    
    RBPipelineHandle get_pipeline_handle() const
    {
        check(vk_pipeline != VK_NULL_HANDLE); 
        return vk_pipeline;
    }
    
    
    void reflect_shader(const VkShader& shader, ShaderStage stage)
    {
        SpirvReflection reflection(shader.get_spirv());
    
        pipeline_reflection.insert({stage, 
            PipelineReflection(
                shader.get_shader_name(),
                reflection.get_input_variables(), 
                reflection.get_bindings())});
    }

    const std::map<ShaderStage, PipelineReflection>& get_reflection() const
    {
        return pipeline_reflection;
    }
    
    vk::Instance& instance;
    vk::SwapchainControl& swapchain;
    vk::BufferManager& buffer_manager;

    VkPipeline vk_pipeline = VK_NULL_HANDLE;
    
    std::vector<VkShader> shaders;
    
    std::map<ShaderStage, PipelineReflection> pipeline_reflection;
};
