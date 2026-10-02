module vk;

import :render_resource_instance;

import std.compat;
import assertions;
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

    buffer_manager.update_any_buffer(buffer, size, data, frame);
}

void VkRenderResourceInstance::update_image(Name buffer_name, RBImageHandle image_handle, const UpdateImageParams& update_params)
{
    PROFILE("VkRenderResourceInstance::update_image");
    
    auto inst_info = resource->backend.pipeline_manager.resource_instance_data.at(this);
    auto& pipe_info = resource->backend.pipeline_manager.resources_info.at(resource);

    auto [binding_index, binding] =
        pipe_info.descritor_set_layout_desc.get_binding(buffer_name);

    checkf(binding.parameter.type == MaterialParamType::sampler || binding.parameter.type == MaterialParamType::image, 
        "Type mismatch");

    uint32_t frame_index = usage.frame_index(update_params.frame);

    RBDescriptorSet set = inst_info.sets_per_frame[frame_index];
    
    if (binding.parameter.type == MaterialParamType::sampler)
    {
        resource->backend.update_sampled_image(
            set,
            *binding.binding_index,
            image_handle,
            usage,
            binding.sampler,
            update_params.layer_index,
            update_params.array_layers,
            update_params.cubemap,
            update_params.array_index,
            update_params.as_array_2d);
        return;
    }
    
    if (binding.parameter.type == MaterialParamType::image)
    {
        resource->backend.update_storage_image(
            set, *binding.binding_index, image_handle,
            update_params.array_index,
            update_params.as_array_2d);
        return;
    }

    unreachable("invalid parameter type");
}

void VkRenderResourceInstance::update_tlas(Name name, RBAccelStruct tlas, RBFrameHandle frame)
{
    auto var = resource->desc.find_variable_checked(name);
    
    checkf(var.parameter.type == MaterialParamType::tlas, "Type mismatch");
    
    auto inst_info = resource->backend.pipeline_manager.resource_instance_data.at(this);

    auto& pipe_info = resource->backend.pipeline_manager.resources_info.at(resource);

    auto [binding_index, binding] = pipe_info.descritor_set_layout_desc.get_binding(name);

    uint32_t frame_index = usage.frame_index(frame);

    RBDescriptorSet set = inst_info.sets_per_frame[frame_index];

    resource->backend.update_tlas(
        set,
        *binding.binding_index,
        tlas);
}

void VkRenderResourceInstance::update_ssbo(Name buffer_name, size_t size, void* data, std::optional<RBFrameHandle> frame)
{
    auto inst_info = resource->backend.pipeline_manager.resource_instance_data.at(this);
    auto& pipe_info = resource->backend.pipeline_manager.resources_info.at(resource);

    auto [binding_index, binding] = pipe_info.descritor_set_layout_desc.get_binding(buffer_name);
    checkf(binding.parameter.type == MaterialParamType::ssbo, "Type mismatch");

    uint32_t frame_index = usage.frame_index(frame);

    RBBufferHandle buffer = inst_info.buffers[binding_index][frame_index];
    buffer_manager.update_any_buffer(buffer, size, data, frame_index);
}

void VkRenderResourceInstance::update_ssbo_element(Name buffer_name, size_t element_size, size_t index, const void* data,
                                                   std::optional<RBFrameHandle> frame)
{
    auto inst_info = resource->backend.pipeline_manager.resource_instance_data.at(this);
    auto& pipe_info = resource->backend.pipeline_manager.resources_info.at(resource);

    auto [binding_index, binding] = pipe_info.descritor_set_layout_desc.get_binding(buffer_name);
    checkf(binding.parameter.type == MaterialParamType::ssbo, "Type mismatch");

    uint32_t frame_index = usage.frame_index(frame);

    RBBufferHandle buffer = inst_info.buffers[binding_index][frame_index];

    buffer_manager.update_buffer_element(buffer, element_size, index, data, frame_index);
}

RBBufferHandle VkRenderResourceInstance::get_ssbo_handle(Name buffer_name, std::optional<RBFrameHandle> frame)
{
    auto& inst_info = resource->backend.pipeline_manager.resource_instance_data.at(this);
    auto& pipe_info = resource->backend.pipeline_manager.resources_info.at(resource);

    auto [binding_index, binding] = pipe_info.descritor_set_layout_desc.get_binding(buffer_name);
    checkf(binding.parameter.type == MaterialParamType::ssbo, "Type mismatch");

    return inst_info.buffers[binding_index][usage.frame_index(frame)];
}

std::span<std::byte> VkRenderResourceInstance::map_ssbo(Name buffer_name, std::optional<RBFrameHandle> frame)
{
    const RBBufferHandle handle = get_ssbo_handle(buffer_name, frame);
    vk::BufferInfo& buf = buffer_manager.get_buffer(handle, usage.frame_index(frame));
    checkf(buf.mapped_ptr, "Storage buffer '%s' is not host visible", buffer_name.to_string().c_str());
    return { static_cast<std::byte*>(buf.mapped_ptr), (size_t)buf.size };
}

void VkRenderResourceInstance::bind(RBCommandList command_list, RBFrameHandle frame)
{
    PROFILE("VkRenderResourceInstance::bind");
    
    if (!has_set_per_frame)
    {
        vk::PipelineManager::ResourceInstanceData inst_info = resource->backend.pipeline_manager.resource_instance_data.at(this);
        checkf(array_size(set_per_frame) >= inst_info.sets_per_frame.size(),
            "Array too large");
        for (int i = 0; i < inst_info.sets_per_frame.size(); ++i)
        {
            set_per_frame[i] = inst_info.sets_per_frame[i];
        }
        has_set_per_frame = true;
    }
    // sets are numbered per pipeline layout: the same resource can be set 2 in one pipeline and 7 in another
    const std::optional<uint32_t> set_index = resource->backend.pipeline_manager.find_resource_set(resource);
    checkf(set_index.has_value(), "resource %s is not in the layout of the bound pipeline (add it to the pipeline's stage resources)",
        resource->desc.name.to_string().c_str());

    const uint32_t frame_index = usage.frame_index(frame);

    const RBDescriptorSet set = set_per_frame[frame_index];
    

    resource->backend.bind_descriptor_set(
        command_list,
        *set_index,
        set,
        resource->desc.name);
}