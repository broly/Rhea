module vk:render_resource_instance;
import :render_resource;
import :pipeline;
import :render_backend;
#include "common/assertion_macros.h"

VkRenderResourceInstance::VkRenderResourceInstance(vk::BufferManager& buffer_manager, const VkRenderResource& in_resource, ResourceUsageType in_usage)
    : buffer_manager(buffer_manager)
    , resource(in_resource)
    , usage(in_usage)
{}

void VkRenderResourceInstance::update_uniform_buffer_impl(PipelineObject* pipeline_object,
                                                          const char* buffer_name_SUBOPTIMAL, size_t size, void* data, RBFrameHandle frame)
{
    auto vk_pipeline_object = (VkPipelineObject*)pipeline_object;
    const auto& info = resource.info_by_pipeline.find(vk_pipeline_object);
    
    auto [index, binding] = info->second.descritor_set_layout_desc.get_binding(buffer_name_SUBOPTIMAL);
    checkf(binding.type == DescriptorType::UniformBuffer, "type mismatch");
    auto ds_idx = usage == ResourceUsageType::frame ? frame : 0;
    
    buffer_manager.update_uniform_buffer(info->second.buffers[index][ds_idx], size, data, frame);
}

void VkRenderResourceInstance::update_image(class PipelineObject* pipeline_object, const char* buffer_name_SUBOPTIMAL,
                                            RBImageHandle image_handle, RBFrameHandle frame)
{
    auto vk_pipeline_object = (VkPipelineObject*)pipeline_object;
    const auto& info_it = resource.info_by_pipeline.find(vk_pipeline_object);
    auto& info = info_it->second;
    auto [index, binding] = info.descritor_set_layout_desc.get_binding(buffer_name_SUBOPTIMAL);
    checkf(binding.type == DescriptorType::CombinedImageSampler, "type mismatch");
    checkf(info.sets_per_frame.size() > 0, "Descriptor set not allocated for resource instance");
    
    auto ds_idx = usage == ResourceUsageType::frame ? frame : 0;
    RBDescriptorSet descriptor_set = info.sets_per_frame[ds_idx];
    
    resource.backend.update_sampled_image(descriptor_set, binding.binding_index, image_handle, usage);
}

void VkRenderResourceInstance::bind(class PipelineObject* pipeline_object, RBCommandList command_list, RBFrameHandle frame)
{
    VkPipelineObject* vk_pipeline_object = (VkPipelineObject*)pipeline_object;
    const auto& info_it = resource.info_by_pipeline.find(vk_pipeline_object);
    auto& info = info_it->second;

    checkf(info.sets_per_frame.size() > 0, "Descriptor set not allocated for resource instance");
    
    auto ds_idx = usage == ResourceUsageType::frame ? frame : 0;
    RBDescriptorSet descriptor_set = info.sets_per_frame[ds_idx];
    resource.backend.bind_descriptor_set(
        command_list,
        info.descritor_set_layout_desc.set_index,
        descriptor_set,
        pipeline_object->get_pipeline_handle()
    );
}
