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

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

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

        g.generalShader = group.general_shader >= 0 ? group.general_shader : VK_SHADER_UNUSED_KHR;

        g.closestHitShader = group.closest_hit_shader >= 0 ? group.closest_hit_shader : VK_SHADER_UNUSED_KHR;

        g.anyHitShader = group.any_hit_shader >= 0 ? group.any_hit_shader : VK_SHADER_UNUSED_KHR;

        g.intersectionShader = group.intersection_shader >= 0 ? group.intersection_shader : VK_SHADER_UNUSED_KHR;

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
    
    create_sbt();

    return vk_pipeline;
}

void VkPipelineObject_RayTrace::create_sbt()
{
    auto& props = instance.get_rt_props();

    uint32_t handle_size = props.shaderGroupHandleSize;

    uint32_t handle_stride =
        align_up(handle_size, props.shaderGroupHandleAlignment);

    uint32_t region_stride =
        align_up(handle_stride, props.shaderGroupBaseAlignment);

    uint32_t raygen_count = pipeline_desc.sbt_raygen.size();
    uint32_t miss_count   = pipeline_desc.sbt_miss.size();
    uint32_t hit_count    = pipeline_desc.sbt_hit.size();
    uint32_t call_count   = pipeline_desc.sbt_callable.size();

    uint32_t group_count =
        raygen_count +
        miss_count +
        hit_count +
        call_count;

    uint32_t sbt_size = region_stride * group_count;

    std::vector<uint8_t> handles(
        pipeline_desc.groups.size() * handle_size);

    vk_ext::vkGetRayTracingShaderGroupHandlesKHR(
        instance.device,
        vk_pipeline,
        0,
        pipeline_desc.groups.size(),
        handles.size(),
        handles.data());

    vk::create_buffer(
        instance.device,
        instance.physical_device,
        sbt_size,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        sbt_buffer,
        sbt_memory);

    void* data;

    vkMapMemory(instance.device, sbt_memory, 0, sbt_size, 0, &data);

    uint8_t* dst = reinterpret_cast<uint8_t*>(data);

    uint32_t offset = 0;

    auto write_region =
    [&](const std::vector<uint32_t>& groups)
    {
        for (auto g : groups)
        {
            memcpy(
                dst + offset * region_stride,
                handles.data() + g * handle_size,
                handle_size);

            offset++;
        }
    };

    write_region(pipeline_desc.sbt_raygen);
    write_region(pipeline_desc.sbt_miss);
    write_region(pipeline_desc.sbt_hit);
    write_region(pipeline_desc.sbt_callable);

    vkUnmapMemory(instance.device, sbt_memory);

    VkBufferDeviceAddressInfo addr_info{
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO
    };

    addr_info.buffer = sbt_buffer;

    VkDeviceAddress sbt_address =
        vkGetBufferDeviceAddress(instance.device, &addr_info);

    uint32_t raygen_offset = 0;
    uint32_t miss_offset   = raygen_offset + raygen_count;
    uint32_t hit_offset    = miss_offset + miss_count;
    uint32_t call_offset   = hit_offset + hit_count;

    raygen_region.deviceAddress =
        sbt_address + raygen_offset * region_stride;

    raygen_region.stride = region_stride;
    raygen_region.size   = region_stride * raygen_count;

    miss_region.deviceAddress =
        sbt_address + miss_offset * region_stride;

    miss_region.stride = region_stride;
    miss_region.size   = region_stride * miss_count;

    hit_region.deviceAddress =
        sbt_address + hit_offset * region_stride;

    hit_region.stride = region_stride;
    hit_region.size   = region_stride * hit_count;

    callable_region.deviceAddress =
        sbt_address + call_offset * region_stride;

    callable_region.stride = region_stride;
    callable_region.size   = region_stride * call_count;
}