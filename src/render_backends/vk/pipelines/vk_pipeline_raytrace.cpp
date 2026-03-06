module vk:pipeline_raytrace;

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
import :device_extension_api;
#include "common/assertion_macros.h"

class VkRenderBackend;

VkPipelineObject_RayTrace::VkPipelineObject_RayTrace(
    vk::Instance& in_instance,
    vk::SwapchainControl& in_swapchain,
    vk::BufferManager& in_buffer_manager,
    RBPipelineLayout pipeline_layout,
    const PipelineCreateDesc_RayTrace& desc)
        : VkPipelineObject(in_instance, in_swapchain, in_buffer_manager, pipeline_layout)
        , pipeline_desc(desc)
{
    debug_name = desc.pass_name;
    permutation_value = desc.permutation_value;
}

VkPipeline VkPipelineObject_RayTrace::create_pipeline(VkRenderPass)
{
    std::vector<VkPipelineShaderStageCreateInfo> vk_stages;

    for (auto& stage : pipeline_desc.stages)
    {

        VkShader shader(instance.device, stage.compiled_shader);

        reflect_shader(shader, stage.stage);

        shaders.push_back(std::move(shader));

        VkPipelineShaderStageCreateInfo vk_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
        };

        vk_stage.stage  = vk_conf_converters::conv_shader_stage(stage.stage);
        vk_stage.module = shaders.back().get_module();
        vk_stage.pName  = "main";

        vk_stages.push_back(vk_stage);
    }

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> vk_groups;

    for (auto& group : pipeline_desc.groups)
    {
        VkRayTracingShaderGroupCreateInfoKHR g{
            VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR
        };

        g.generalShader      = group.general_shader;
        g.closestHitShader   = group.closest_hit_shader;
        g.anyHitShader       = group.any_hit_shader;
        g.intersectionShader = group.intersection_shader;

        switch (group.type)
        {
        case RayTracingGroupType::general:
            g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            break;

        case RayTracingGroupType::triangles_hit:
            g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            break;

        case RayTracingGroupType::procedural_hit:
            g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
            break;

        default:
            checkf(false, "unknown ray tracing group type");
        }

        vk_groups.push_back(g);
    }

    VkRayTracingPipelineCreateInfoKHR pci{
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR
    };

    pci.stageCount = (uint32_t)vk_stages.size();
    pci.pStages = vk_stages.data();

    pci.groupCount = (uint32_t)vk_groups.size();
    pci.pGroups = vk_groups.data();

    pci.maxPipelineRayRecursionDepth = pipeline_desc.max_recursion_depth;

    pci.layout = pipeline_layout;


    VK_CHECK(vk_ext::vkCreateRayTracingPipelinesKHR(
        instance.device,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        1,
        &pci,
        nullptr,
        &vk_pipeline));

    LogVkPipeline.Log<Display>(
        "Created ray tracing pipeline %p (pass: %s)",
        vk_pipeline,
        pipeline_desc.pass_name.to_string().c_str());

    return vk_pipeline;
}