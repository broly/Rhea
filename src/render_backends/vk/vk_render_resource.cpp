module vk:render_resource;

import <cassert>;
import <set>;

#include "common/assertion_macros.h"

#define check_conditional(cond, assertion, msg, ...) \
        if (cond) \
            checkf(assertion, msg, __VA_ARGS__); \
            
DescriptorType TEMP_CONVERT_DESCRIPTOR_TYPE_CRUTCH(VkDescriptorType descriptor_type)
{
    switch (descriptor_type)
    {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            return DescriptorType::Sampler;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return DescriptorType::CombinedImageSampler;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return DescriptorType::SampledImage;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return DescriptorType::UniformBuffer;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return DescriptorType::StorageBuffer;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        default: 
            break;
    }
    assert(false);
    __assume(false);
}


VkRenderResource::VkRenderResource(const RenderResourceDesc& desc, vk::BufferManager& in_buffer_manager,
    class VkRenderBackend& in_backend)
    : RenderResource(desc)
    , buffer_manager(in_buffer_manager)
    , backend(in_backend)
{}

RenderResourceInstance* VkRenderResource::create_instance()
{
    auto instance = std::make_unique<VkRenderResourceInstance>(
        buffer_manager, *this, desc.usage_type);

    for (auto& [pipeline, info] : info_by_pipeline)
    {
        auto& inst = instance->per_pipeline[pipeline];

        inst.sets_per_frame =
            buffer_manager.allocate_descriptor_sets_for_layout(
                info.layout, desc.usage_type);

        inst.buffers.resize(info.descritor_set_layout_desc.bindings.size());

        for (size_t binding_index = 0;
             binding_index < info.descritor_set_layout_desc.bindings.size();
             ++binding_index)
        {
            const auto& binding =
                info.descritor_set_layout_desc.bindings[binding_index];

            if (binding.type != DescriptorType::UniformBuffer)
                continue;

            auto& buffers_per_frame = inst.buffers[binding_index];

            for (size_t frame = 0; frame < inst.sets_per_frame.size(); ++frame)
            {
                RBBufferHandle buffer =
                    buffer_manager.create_uniform_buffer(binding.size, desc.usage_type);

                buffer_manager.bind_buffer_to_descriptor(
                    inst.sets_per_frame[frame],
                    binding.binding_index,
                    buffer,
                    frame);

                buffers_per_frame.push_back(buffer);
            }
        }
    }

    instances.push_back(std::move(instance));
    return instances.back().get();
}


RenderResourceInstance* VkRenderResource::query_single()
{
    if (single_resource)
        return single_resource;
    auto resource = create_instance();
    single_resource = resource;
    return single_resource;
}

void VkRenderResource::bind(PipelineObject* pipeline_object)
{
}

void VkRenderResource::provide(PipelineObject* pipeline_object)
{
    auto* vk_pipeline = static_cast<VkPipelineObject*>(pipeline_object);
    const auto& reflection = vk_pipeline->get_reflection();

    DescriptorSetLayoutDesc layout_desc{};
    layout_desc.debug_name = desc.name;

    std::optional<uint32_t> set_index;
    std::map<std::string, uint32_t> variable_bindings;
    std::set<uint32_t> occupied_bindings;

    for (const auto& var : desc.variables)
    {
        for (const auto& [stage, refl] : reflection)
        {
            if (!bool(desc.stages & stage) && desc.stages != ShaderStage::none)
                continue;

            auto it = refl.bindings.find(var.name);
            if (it == refl.bindings.end())
                continue;

            const auto& rb = it->second;

            if (!set_index)
                set_index = rb.set;
            else
                checkf(set_index == rb.set, "Different set index for same resource");

            if (occupied_bindings.contains(rb.binding))
                continue;

            DescriptorBinding binding{};
            binding.name          = var.name;
            binding.binding_index = rb.binding;
            binding.count         = rb.count;
            binding.size          = var.size;
            binding.stages        = desc.stages;
            binding.type          = TEMP_CONVERT_DESCRIPTOR_TYPE_CRUTCH(rb.descriptor_type);

            layout_desc.bindings.push_back(binding);
            occupied_bindings.insert(rb.binding);
        }
    }

    layout_desc.set_index = set_index.value_or(0);

    VkRenderResourcePipelineInfo info{};
    info.descritor_set_layout_desc = layout_desc;
    info.layout = buffer_manager.create_descriptor_set_layout(layout_desc);

    info_by_pipeline[vk_pipeline] = info;
}


RBDescriptorSetLayout VkRenderResource::get_descriptor_set_layout(class PipelineObject* pipeline_object)
{
    auto vk_pipeline_object = (VkPipelineObject*)pipeline_object;
    auto info_it = info_by_pipeline.find(vk_pipeline_object);
    auto& info = info_it->second;
    
    return info.layout;
    
}
