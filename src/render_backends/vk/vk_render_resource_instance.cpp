module vk:render_resource_instance;
import :render_resource;
import :render_backend;
import profile;
#include "common/assertion_macros.h"
#include "profiling/profile.h"

VkRenderResourceInstance::VkRenderResourceInstance(
    vk::BufferManager& buffer_manager, 
    const VkRenderResource* in_resource, 
    ResourceUsage in_usage, 
    RBPipelineLayout in_pipeline_layout)
    : buffer_manager(buffer_manager)
    , resource(in_resource)
    , usage(in_usage)
    , pipeline_layout(in_pipeline_layout.as<VkPipelineLayout>())
{}

void VkRenderResourceInstance::update_uniform_buffer_impl(Name buffer_name, size_t size, void* data, RBFrameHandle frame)
{    
    auto inst_info = resource->backend.pipeline_manager.instance_pipeline_data.at(this);
    
    auto& pipe_info = resource->backend.pipeline_manager.resources_pipeline_info.at({resource, RBPipelineLayout(pipeline_layout)});

    auto [binding_index, binding] =
        pipe_info.descritor_set_layout_desc.get_binding(buffer_name);

    checkf(binding.parameter.type == MaterialParamType::uniform, "Type mismatch");

    uint32_t frame_index = usage.frame_index(frame);

    RBBufferHandle buffer =
        inst_info.buffers[binding_index][frame_index];

    buffer_manager.update_uniform_buffer(buffer, size, data, frame);
}

void VkRenderResourceInstance::update_image(Name buffer_name,
                                            RBImageHandle image_handle, RBFrameHandle frame, uint32_t array_index, bool cubemap)
{
    PROFILE("VkRenderResourceInstance::update_image");
    
    
    
    auto inst_info = resource->backend.pipeline_manager.instance_pipeline_data.at(this);
    auto& pipe_info = resource->backend.pipeline_manager.resources_pipeline_info.at({resource, RBPipelineLayout(pipeline_layout)});

    auto [binding_index, binding] =
        pipe_info.descritor_set_layout_desc.get_binding(buffer_name);

    checkf(binding.parameter.type == MaterialParamType::sampler, "Type mismatch");

    uint32_t frame_index = usage.frame_index(frame);

    RBDescriptorSet set = inst_info.sets_per_frame[frame_index];

    resource->backend.update_sampled_image(
        set,
        *binding.binding_index,
        image_handle,
        usage,
        binding.sampler,
        array_index,
        cubemap);
}

void VkRenderResourceInstance::bind(RBCommandList command_list, RBFrameHandle frame)
{
    PROFILE("VkRenderResourceInstance::bind");
    
    auto inst_info = resource->backend.pipeline_manager.instance_pipeline_data.at(this);
    auto& pipe_info = resource->backend.pipeline_manager.resources_pipeline_info.at({resource, RBPipelineLayout(pipeline_layout)});

    const uint32_t frame_index = usage.frame_index(frame);

    const RBDescriptorSet set = inst_info.sets_per_frame[frame_index];
    

    resource->backend.bind_descriptor_set(
        command_list,
        pipe_info.descritor_set_layout_desc.set_index,
        set,
        pipeline_layout,
        resource->desc.name);
}
