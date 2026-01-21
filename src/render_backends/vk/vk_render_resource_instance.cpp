module vk:render_resource_instance;
import :render_resource;
import :render_backend;
import profile;
#include "common/assertion_macros.h"
#include "profiling/profile.h"

VkRenderResourceInstance::VkRenderResourceInstance(vk::BufferManager& buffer_manager, const VkRenderResource& in_resource, ResourceUsageType in_usage)
    : buffer_manager(buffer_manager)
    , resource(in_resource)
    , usage(in_usage)
{}

void VkRenderResourceInstance::update_uniform_buffer_impl(PipelineObject* pipeline_object,
                                                          Name buffer_name, size_t size, void* data, RBFrameHandle frame)
{
    auto* vk_pipeline    = static_cast<VkPipelineObject*>(pipeline_object);
    auto& pipe_info = resource.info_by_pipeline.at(vk_pipeline);
    auto& inst_info = per_pipeline.at(vk_pipeline);

    auto [binding_index, binding] =
        pipe_info.descritor_set_layout_desc.get_binding(buffer_name);

    checkf(binding.type == DescriptorType::UniformBuffer, "Type mismatch");

    uint32_t frame_index = usage == ResourceUsageType::frame ? frame : 0;

    RBBufferHandle buffer =
        inst_info.buffers[binding_index][frame_index];

    buffer_manager.update_uniform_buffer(buffer, size, data, frame);
}

void VkRenderResourceInstance::update_image(class PipelineObject* pipeline_object, Name buffer_name,
                                            RBImageHandle image_handle, RBFrameHandle frame)
{
    auto* vk_pipeline = static_cast<VkPipelineObject*>(pipeline_object);
    auto& pipe_info = resource.info_by_pipeline.at(vk_pipeline);
    auto& inst_info = per_pipeline.at(vk_pipeline);

    auto [binding_index, binding] =
        pipe_info.descritor_set_layout_desc.get_binding(buffer_name);

    checkf(binding.type == DescriptorType::CombinedImageSampler, "Type mismatch");

    uint32_t frame_index = usage == ResourceUsageType::frame ? frame : 0;

    RBDescriptorSet set = inst_info.sets_per_frame[frame_index];

    resource.backend.update_sampled_image(
        set,
        binding.binding_index,
        image_handle,
        usage,
        resource.desc.sampler);
}

void VkRenderResourceInstance::bind(class PipelineObject* pipeline_object, RBCommandList command_list, RBFrameHandle frame)
{
    PROFILE("VkRenderResourceInstance::bind");
    auto* vk_pipeline = static_cast<VkPipelineObject*>(pipeline_object);
    auto& pipe_info = resource.info_by_pipeline.at(vk_pipeline);
    auto& inst_info = per_pipeline.at(vk_pipeline);

    uint32_t frame_index = usage == ResourceUsageType::frame ? frame : 0;

    RBDescriptorSet set = inst_info.sets_per_frame[frame_index];

    resource.backend.bind_descriptor_set(
        command_list,
        pipe_info.descritor_set_layout_desc.set_index,
        set,
        vk_pipeline->get_pipeline_handle());
}
