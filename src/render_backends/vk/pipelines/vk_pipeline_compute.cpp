module vk:pipeline_compute;

import render;
import :helpers;
import rhmath;
import <vector>;
import <vulkan/vulkan_core.h>;
import <cassert>;
import spirv_reflect;
import :reflection;
import :render_resource;
import :log;
#include "../vk_macro.h"
import reflect;
import :enums_adapters;
import <bit>;
import <set>;
import :pipeline_helpers;
#include "common/assertion_macros.h"

class VkRenderBackend;

VkPipelineObject_Compute::VkPipelineObject_Compute(
    vk::Instance& in_instance,
    vk::SwapchainControl& in_swapchain,
    vk::BufferManager& in_buffer_manager,
    RBPipelineLayout pipeline_layout,
    const PipelineCreateDesc_Compute& desc)
        : VkPipelineObject(in_instance, in_swapchain, in_buffer_manager, pipeline_layout)
        , pipeline_desc(desc)
{
    debug_name = desc.pass_name;
    permutation_value = desc.permutation_value;
}

VkPipeline VkPipelineObject_Compute::create_pipeline(VkRenderPass render_pass)
{
    checkf(pipeline_desc.stages.size() == 1, "compute shader missing");
    
    const auto& stage_info = pipeline_desc.stages[0];

    VkShader shader(instance.device, *stage_info.compiled_shader);

    reflect_shader(shader, ShaderStage::compute);

    shaders.push_back(std::move(shader));

    VkPipelineShaderStageCreateInfo stage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
    };

    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shaders[0].get_module();
    stage.pName = "main";

    VkComputePipelineCreateInfo pci{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO
    };

    pci.stage = stage;
    pci.layout = pipeline_layout;

    VK_CHECK(vkCreateComputePipelines(
        instance.device,
        VK_NULL_HANDLE,
        1,
        &pci,
        nullptr,
        &vk_pipeline));

    LogVkPipeline.Log("Created compute pipeline %p (%s)",
        vk_pipeline,
        debug_name.to_string().c_str());

    return vk_pipeline;
}

