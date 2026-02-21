module vk:pipeline_manager;
import :helpers;
import <vector>;
import :log;
import <vulkan/vulkan_core.h>;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogVkPipelineManager, Warning);


PipelineObject* vk::PipelineManager::create_pipeline(const GraphicsPipelineDesc& desc)
{
    std::unique_ptr<VkPipelineObject> pipeline = std::make_unique<VkPipelineObject>(instance, swapchain, buffer_manager, desc);
    PipelineObject* result = pipeline.get();
    pending_pipelines.push_back(std::move(pipeline));
    return result;
}

RBPipelineLayout vk::PipelineManager::create_layout(const PipelineLayoutDesc& desc)
{
    
    VkPipelineLayout pipeline_layout;
    
    std::vector<VkDescriptorSetLayout> vk_layouts;

    std::vector<VkRenderResourcePipelineInfo> prepared_info;
    for (auto& resource_info : desc.resources)
    {
        DescriptorSetLayoutDesc layout_desc;
        const RenderResourceDesc& resource_desc = resource_info.resource->desc;
        layout_desc.debug_name = resource_info.name;
        layout_desc.set_index = resource_info.set;
        for (auto& binding :  resource_info.resource_variable_bindings)
        {
            if (binding.is_descriptor())
                layout_desc.bindings.push_back(binding);
            
        }
        
    
        VkRenderResourcePipelineInfo info{};
        info.descritor_set_layout_desc = layout_desc;
        layout_desc.debug_name = resource_desc.name;
        info.layout = buffer_manager.create_descriptor_set_layout(layout_desc, desc.pass);
        vk_layouts.push_back(buffer_manager.get_vk_descriptor_set_layout(info.layout).vk_layout);
        prepared_info.push_back(info);
    }
    
    VkPipelineLayoutCreateInfo plci{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    plci.setLayoutCount = uint32_t(vk_layouts.size());
    plci.pSetLayouts = vk_layouts.data();

    // push constants
    plci.pushConstantRangeCount = desc.push_constants.size();
    std::vector<VkPushConstantRange> push_constants = vk::to_vk_ranges(desc.push_constants);
    plci.pPushConstantRanges = push_constants.data();

    vkCreatePipelineLayout(instance.device, &plci, nullptr, &pipeline_layout);
    LogVkPipeline.Log("Created pipeline layout %s (%p)", desc.pass.to_string().c_str(), pipeline_layout);
    
    
    RBPipelineLayout result {pipeline_layout};
    
    for (int index = 0; auto& resource_info : desc.resources)
    {
        
        LogVkPipeline.Log(" * resource %s", resource_info.name.to_string().c_str());
        UniqueResourcePair pair = {(VkRenderResource*)resource_info.resource, result};
        resources_pipeline_info.insert({pair, prepared_info.at(index)});
        index++;
    }
    
    instance_data.insert({result, {desc}});
    
    
    return result;
}

std::shared_ptr<VkRenderResourceInstance> vk::PipelineManager::query_single_resource_instance(VkRenderResource* resource, RBPipelineLayout pipeline_layout, uint32_t unique_id, uint32_t instance_id, ResourceUsage
    usage)
{
    auto& instances_list = unique_resource_instances[{resource, pipeline_layout, unique_id}];
    
    if (instance_id >= instances_list.size())
        instances_list.resize(instance_id + 1, nullptr);
    
    if (instances_list[instance_id] != nullptr)
        return instances_list[instance_id];
    
    std::shared_ptr<VkRenderResourceInstance> resource_instance = 
        std::make_shared<VkRenderResourceInstance>(buffer_manager, resource, usage, pipeline_layout);
    instances_list[instance_id] = resource_instance;
    
    update_buffers();
    
    return instances_list[instance_id];
}

void vk::PipelineManager::push_constants(const RBCommandList& cmd, const void* data, size_t size,
                                         RBPipelineLayout pipeline_layout)
{
    PROFILE(__FUNCTION__);

    const PipelineLayoutDesc& pipeline_layout_desc = get_pipeline_layout_desc(pipeline_layout);
    
    uint32_t stage_bits = 0;
    
    for (auto& stage : pipeline_layout_desc.push_constants)
    {
        if ((stage.stages & ShaderStage::fragment) != ShaderStage::none)
            stage_bits |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if ((stage.stages & ShaderStage::vertex) != ShaderStage::none)
            stage_bits |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    
    auto vk_pipeline_layout = pipeline_layout.as<VkPipelineLayout>();

    vkCmdPushConstants(
        cmd,
        vk_pipeline_layout,
        VkShaderStageFlagBits(stage_bits), 
        0, 
        size,
        data
    );
    
}

void vk::PipelineManager::bind_descriptor_set(RBCommandList cmd_list, int set_index, RBDescriptorSet rb_descriptors,
                                              RBPipelineLayout pipeline_layout, Name debug_name)
{
    
    PROFILE("bind_descriptor_set");
    
    VkDescriptorSet vk_set = rb_descriptors.as<VkDescriptorSet>();
    
    if (vk_set == current_descriptor_set && current_pipeline_layout == pipeline_layout)
        return;
    
    LogVkPipelineManager.Log("bind_descriptor_set. cmd: %p, descriptor_set: %p, pipeline_layout: %p (%s)",
        cmd_list, rb_descriptors, pipeline_layout, debug_name.to_string().c_str());
    
    current_descriptor_set = vk_set;
    current_pipeline_layout = pipeline_layout;
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    
    auto vk_pipeline_layout = pipeline_layout.as<VkPipelineLayout>();
    
    {
        PROFILE("vkCmdBindDescriptorSets");
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            vk_pipeline_layout,
            set_index, 
            1, 
            &vk_set,
            0, nullptr
        );
    }
}

void vk::PipelineManager::bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object, VkRenderPass current_render_pass)
{
    
    PROFILE("VkRenderBackend::bind_pipeline");
    LogRB.Log<Verbose>("bind_pipeline");
    
    auto vk_pipeline_object = static_cast<VkPipelineObject*>(pipeline_object);
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    checkf(current_render_pass != VK_NULL_HANDLE, "Invalid render pass");
    VkPipeline pipeline_vk = vk_pipeline_object->get_or_create_pipeline(current_render_pass);
    RBPipelineHandle handle = pipeline_vk;
    
    for (auto it = pending_pipelines.begin(); it != pending_pipelines.end(); ++it)
    {
        if (it->get() == pipeline_object)
        {
            std::unique_ptr<VkPipelineObject> pipeline_ptr = std::move(*it);
            pending_pipelines.erase(it);
            pipelines.insert({handle, std::move(pipeline_ptr)});
            break;
        }
    }

    checkf(pipeline_vk != VK_NULL_HANDLE, "pipeline not created");
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_vk);
}

const PipelineLayoutDesc& vk::PipelineManager::get_pipeline_layout_desc(RBPipelineLayout layout)
{
    return instance_data.at(layout).desc;
}

void vk::PipelineManager::update_buffers()
{
    for (const auto& [resource_layout_id, instances] : unique_resource_instances)
    {
        auto [resource, layout, unique_id] = resource_layout_id;
        for (std::shared_ptr<VkRenderResourceInstance> instance : instances)
        {
            VkRenderResourceInstance* instance_raw = instance.get();
            const auto info_it = resources_pipeline_info.find({resource, layout});
            
            checkf(info_it != resources_pipeline_info.end(), "Resource pair (%s, %p) not found",
                resource->desc.name.to_string().c_str(), (void*)layout.handle );
            
            auto info = info_it->second;

            if (instance_pipeline_data.contains(instance.get()))
                continue;
        
            if (!instance_pipeline_data.contains(instance_raw))
                instance_pipeline_data.insert({instance_raw, {}});
            auto& inst = instance_pipeline_data.at(instance_raw);

            inst.sets_per_frame =
                buffer_manager.allocate_descriptor_sets_for_layout(
                    info.layout, resource->desc.usage, resource->desc.name);

            inst.buffers.resize(info.descritor_set_layout_desc.bindings.size());

            for (size_t binding_index = 0;
                 binding_index < info.descritor_set_layout_desc.bindings.size();
                 ++binding_index)
            {
                const auto& binding =
                    info.descritor_set_layout_desc.bindings[binding_index];

                if (binding.parameter.type != MaterialParamType::uniform)
                    continue;

                auto& buffers_per_frame = inst.buffers[binding_index];

                for (size_t frame = 0; frame < inst.sets_per_frame.size(); ++frame)
                {
                    RBBufferHandle buffer = buffer_manager.create_uniform_buffer(*binding.parameter.size, resource->desc.usage);

                    buffer_manager.bind_buffer_to_descriptor(
                        inst.sets_per_frame[frame],
                        *binding.binding_index,
                        buffer,
                        frame);

                    buffers_per_frame.push_back(buffer);
                }
            }
        }
    }
}
