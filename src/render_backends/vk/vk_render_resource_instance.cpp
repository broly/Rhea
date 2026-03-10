module vk:render_resource_instance;
import :render_resource;
import :render_backend;
import profile;
import array_helpers;
#include "common/assertion_macros.h"
#include "profiling/profile.h"

VkRenderResourceInstance::VkRenderResourceInstance(
    vk::BufferManager& buffer_manager, 
    const VkRenderResource* in_resource, 
    ResourceUsage in_usage)
    : buffer_manager(buffer_manager)
    , resource(in_resource)
    , usage(in_usage)
{}

void VkRenderResourceInstance::update_uniform_buffer_impl(Name buffer_name, size_t size, void* data, RBFrameHandle frame)
{    
    auto inst_info = resource->backend.pipeline_manager.resource_instance_data.at(this);

    vk::VkRenderResourceInfo& resource_info = resource->backend.pipeline_manager.resources_info.at(resource);

    auto [binding_index, binding] =
        resource_info.descritor_set_layout_desc.get_binding(buffer_name);

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
    
    
    
    auto inst_info = resource->backend.pipeline_manager.resource_instance_data.at(this);
    auto& pipe_info = resource->backend.pipeline_manager.resources_info.at(resource);

    auto [binding_index, binding] =
        pipe_info.descritor_set_layout_desc.get_binding(buffer_name);

    checkf(binding.parameter.type == MaterialParamType::sampler || binding.parameter.type == MaterialParamType::image, 
        "Type mismatch");

    uint32_t frame_index = usage.frame_index(frame);

    RBDescriptorSet set = inst_info.sets_per_frame[frame_index];
    
    if (binding.parameter.type == MaterialParamType::sampler)
    {
        resource->backend.update_sampled_image(
            set,
            *binding.binding_index,
            image_handle,
            usage,
            binding.sampler,
            array_index,
            cubemap);
        return;
    }
    
    if (binding.parameter.type == MaterialParamType::image)
    {
        resource->backend.update_storage_image(
            set, *binding.binding_index, image_handle);
        return;
    }

    unreachable("invalid parameter type");
}

void VkRenderResourceInstance::update_tlas(Name name, RBAccelStruct tlas, RBFrameHandle frame)
{
    auto inst_info =
        resource->backend.pipeline_manager.resource_instance_data.at(this);

    auto& pipe_info =
        resource->backend.pipeline_manager.resources_info.at(resource);

    auto [binding_index, binding] =
        pipe_info.descritor_set_layout_desc.get_binding(name);

    uint32_t frame_index = usage.frame_index(frame);

    RBDescriptorSet set = inst_info.sets_per_frame[frame_index];

    resource->backend.update_tlas(
        set,
        *binding.binding_index,
        tlas);
}

void VkRenderResourceInstance::bind(RBCommandList command_list, RBFrameHandle frame)
{
    PROFILE("VkRenderResourceInstance::bind");
    
    if (!has_set_per_frame)
    {
        vk::PipelineManager::ResourceInstanceData inst_info = resource->backend.pipeline_manager.
                                                                               resource_instance_data.at(this);
        checkf(array_size(set_per_frame) >= inst_info.sets_per_frame.size(),
            "Array too large");
        for (int i = 0; i < inst_info.sets_per_frame.size(); ++i)
        {
            set_per_frame[i] = inst_info.sets_per_frame[i];
        }
        has_set_per_frame = true;
    }
    if (!set_index.has_value())
    {
        vk::VkRenderResourceInfo& pipe_info = resource->backend.pipeline_manager.resources_info.at(resource);
        
        set_index = pipe_info.descritor_set_layout_desc.set_index;
    }

    const uint32_t frame_index = usage.frame_index(frame);

    const RBDescriptorSet set = set_per_frame[frame_index];
    

    resource->backend.bind_descriptor_set(
        command_list,
        *set_index,
        set,
        resource->desc.name);
}
