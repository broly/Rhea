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
    std::unique_ptr<VkRenderResourceInstance> instance = std::make_unique<VkRenderResourceInstance>(buffer_manager, *this, desc.usage_type);
    
    for (auto& [pipeline, info] : info_by_pipeline)
    {
        info.sets_per_frame = buffer_manager.allocate_descriptor_sets_for_layout(info.layout, desc.usage_type);
        
        for (uint32_t index = 0; auto& binding : info.descritor_set_layout_desc.bindings)
        {
            if (binding.type == DescriptorType::UniformBuffer)
            {
                frame_list<RBBufferHandle> buffers_per_frame;
                for (uint32_t frame = 0; auto set : info.sets_per_frame)
                {
                    RBBufferHandle buffer = buffer_manager.create_uniform_buffer(binding.size, desc.usage_type);
                    buffer_manager.bind_buffer_to_descriptor(set, binding.binding_index, buffer, frame);
                    buffers_per_frame.push_back(buffer);
                    frame++;
                    
                }
                if (info.buffers.size() < index + 1)
                    info.buffers.resize(index + 1, {});
                info.buffers[index] = buffers_per_frame;
            }
            index++;
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

void VkRenderResource::provide(class PipelineObject* pipeline_object)
{
    VkPipelineObject* vk_pipeline_object = static_cast<VkPipelineObject*>(pipeline_object);
    const auto& refl = vk_pipeline_object->get_reflection();
    
    DescriptorSetLayoutDesc descriptor_set{
        .debug_name = desc.name,
    };
    
    std::optional<uint32_t> set_index;
    
    std::map<std::string, uint32_t> variable_bindings;
    
    std::set<uint32_t> occupied_bindings;
    
    bool has_any_bindings = false;
    
    for (auto& variable : desc.variables)
    {
        std::optional<DescriptorType> descriptor_type;
        
        for (const auto& [stage, reflection] : refl)
        {
            if (bool(desc.stages & stage) || desc.stages == ShaderStage::none)  // none means "any"
            {
                
                if (desc.stages != ShaderStage::none) 
                    checkf(refl.contains(stage), 
                        "Shader '%s' must have requested stage '%s' for resource '%s'", 
                        reflection.shader_name.c_str(), to_string(stage), desc.name.c_str());;
            
                auto binding_it = reflection.bindings.find(variable.name);
            
                const bool has_binding = binding_it != reflection.bindings.end();

                if (desc.stages != ShaderStage::none) 
                    checkf(has_binding, 
                        "Can't find variable '%s' in shader '%s' for resource '%s', but it is in requested stage '%s'", 
                        variable.name.c_str(), reflection.shader_name.c_str(), desc.name.c_str(), to_string(stage));
            
                has_any_bindings |= has_binding;
            
                if (has_binding)
                {
                    auto& refl_binding = binding_it->second;
                    
                    
                    if (variable_bindings.contains(variable.name))
                    {
                        checkf(variable_bindings[variable.name] == refl_binding.binding,
                            "Variable %s has different bindings in shaders in same pipeline",
                            variable.name.c_str());
                    }
                    variable_bindings.emplace(variable.name, refl_binding.binding);
                
                    if (set_index.has_value())
                        checkf(set_index.value() == refl_binding.set,
                                "Various shaders has different set for same resource '%s'",
                               desc.name.c_str());
                    
                
                    set_index = refl_binding.set;
                
                    if (descriptor_type.has_value())
                        checkf(descriptor_type.value() == TEMP_CONVERT_DESCRIPTOR_TYPE_CRUTCH(refl_binding.descriptor_type),
                            "Different shaders has different types for resource %s", desc.name.c_str());
                    
                    if (!occupied_bindings.contains(refl_binding.binding))
                    {
                        DescriptorBinding desc_binding;
                        desc_binding.binding_index = refl_binding.binding;
                        desc_binding.stages = desc.stages;
                        desc_binding.count = refl_binding.count;
                        desc_binding.type = TEMP_CONVERT_DESCRIPTOR_TYPE_CRUTCH(refl_binding.descriptor_type);
                        desc_binding.size = variable.size;
                        desc_binding.name = variable.name;
                        descriptor_type = desc_binding.type;
                        descriptor_set.bindings.push_back(desc_binding);
                        
                        occupied_bindings.insert(desc_binding.binding_index);
                    }
                }
            }
        }
    }
    
    if (desc.stages != ShaderStage::none)
    {
        checkf(has_any_bindings,
           "Requested resource '%s' has no any bindings in shaders",
           desc.name.c_str());
    }
    descriptor_set.set_index = set_index.value_or(0);
    
    VkRenderResourcePipelineInfo info;
    info.descritor_set_layout_desc = descriptor_set;
    info.layout = buffer_manager.create_descriptor_set_layout(descriptor_set);
    info_by_pipeline.insert({vk_pipeline_object, info});
}

RBDescriptorSetLayout VkRenderResource::get_descriptor_set_layout(class PipelineObject* pipeline_object)
{
    auto vk_pipeline_object = (VkPipelineObject*)pipeline_object;
    auto info_it = info_by_pipeline.find(vk_pipeline_object);
    auto& info = info_it->second;
    
    return info.layout;
    
}
