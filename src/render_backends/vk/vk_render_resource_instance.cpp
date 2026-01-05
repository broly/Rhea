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
                                                          const char* buffer_name_SUBOPTIMAL, size_t size, void* data)
{
    auto vk_pipeline_object = (VkPipelineObject*)pipeline_object;
    const auto& info = resource.info_by_pipeline.find(vk_pipeline_object);
    
    auto  [index, binding] = info->second.descritor_set_layout_desc.get_binding(buffer_name_SUBOPTIMAL);
        
    buffer_manager.update_uniform_buffer(info->second.buffers[index].value(), size, data);
}

void VkRenderResourceInstance::bind(class PipelineObject* pipeline_object, RBCommandList command_list)
{
    VkPipelineObject* vk_pipeline_object = (VkPipelineObject*)pipeline_object;
    const auto& info_it = resource.info_by_pipeline.find(vk_pipeline_object);
    auto& info = info_it->second;

    RBDescriptorSet descriptor_set = resource.backend.get_descriptor_set(info.layout, usage);
    
    resource.backend.bind_descriptor_set(command_list, info.descritor_set_layout_desc.set_index, descriptor_set, vk_pipeline_object->get_pipeline_handle());
    
}
