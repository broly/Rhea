module vk:pipeline_manager;
import :helpers;
import <vector>;
import :log;
import :pipeline_graphics;
import <vulkan/vulkan_core.h>;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"
#include "render_backends/vk/vk_macro.h"

DEFINE_LOGGER(LogVkPipelineManager, Warning);


PipelineObject* vk::PipelineManager::create_graphics_pipeline(const GraphicsPipelineDesc& desc)
{
    std::unique_ptr<VkPipelineObject_Graphics> pipeline = std::make_unique<VkPipelineObject_Graphics>(instance, swapchain, buffer_manager, desc);
    PipelineObject* result = pipeline.get();
    pending_pipelines.push_back(std::move(pipeline));
    return result;
}

RBPipelineLayout vk::PipelineManager::create_pipeline_layout(const PipelineLayoutDesc& desc)
{
    
    VkPipelineLayout pipeline_layout;
    
    std::vector<VkDescriptorSetLayout> vk_layouts;

    std::vector<VkRenderResourceInfo> prepared_info;
    
    uint32_t max_index = 0;
    for (auto& resource_info : desc.resources)
        max_index = std::max(max_index, resource_info.resource->desc.set_index);
    
    for (uint32_t index = 0; index <= max_index; index++)
        vk_layouts.push_back(get_empty_descriptor_set());
    
    for (auto& resource_info : desc.resources)
    {
        RBDescriptorSetLayout layout_id = get_or_create_resource_descriptor_set_layout(
            (VkRenderResource*)resource_info.resource);
        vk_layouts[resource_info.resource->desc.set_index] = buffer_manager.get_vk_descriptor_set_layout(layout_id).vk_layout;
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
    
    uint32_t index = 0;
    for (auto& vk_layout : vk_layouts)
    {
        bool found_res = false;
        for (auto& resource_info : desc.resources)
        {
            if (resource_info.resource->desc.set_index == index)
            {
                found_res = true;
                LogVkPipeline.Log(" * set %i: %s - %p", 
                    index, resource_info.resource->desc.set.to_string().c_str(), vk_layout);
            }
        }
        if (!found_res)
        {
            LogVkPipeline.Log(" * set %i: %s - %p", index, "[PLACEHOLDER]", vk_layout);
        }
        index++;
    }
    
    //for (auto& resource_info : desc.resources)
    //{
    //    LogVkPipeline.Log(" * set %i: %s", resource_info.resource->desc.set_index, resource_info.resource->desc.set.to_string().c_str());
    //}
    
    instance_data[pipeline_layout] = {desc};
    
    RBPipelineLayout result {pipeline_layout};
    
    return result;
}

VkDescriptorSetLayout vk::PipelineManager::get_empty_descriptor_set()
{
    if (empty_descriptor_set.has_value())
        return *empty_descriptor_set;
    
    VkDescriptorSetLayoutCreateInfo ci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    ci.bindingCount = 0;
    ci.pBindings = nullptr;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(
        instance.device,
        &ci,
        nullptr,
        &layout
    ));
    empty_descriptor_set = layout;
    
    return layout;
}

std::shared_ptr<VkRenderResourceInstance> vk::PipelineManager::query_single_resource_instance(
    VkRenderResource* resource, uint32_t unique_id, uint32_t instance_id, ResourceUsage usage)
{
    auto& instances_list = unique_resource_instances[{resource, unique_id}];
    
    if (instance_id >= instances_list.size())
        instances_list.resize(instance_id + 1, nullptr);
    
    if (instances_list[instance_id] != nullptr)
        return instances_list[instance_id];
    
    std::shared_ptr<VkRenderResourceInstance> resource_instance = 
        std::make_shared<VkRenderResourceInstance>(buffer_manager, resource, usage);
    instances_list[instance_id] = resource_instance;

    get_or_create_resource_descriptor_set_layout(resource);
    
    update_buffers();
    
    return instances_list[instance_id];
}

RBDescriptorSetLayout vk::PipelineManager::get_or_create_resource_descriptor_set_layout(VkRenderResource* resource)
{
    if (resources_info.contains(resource))
        return resources_info.at(resource).layout;
    
    DescriptorSetLayoutDesc layout_desc;
    const RenderResourceDesc& resource_desc = resource->desc;
    layout_desc.debug_name = resource->desc.name;
    layout_desc.set_index = resource_desc.set_index;
    
    for (auto& var_desc : resource->desc.variables)
    {
        if (var_desc.parameter.is_descriptor())
        {
            ResourceBinding rr_var_desc;
            rr_var_desc.parameter = var_desc.parameter;
            rr_var_desc.binding_index = var_desc.binding;
            rr_var_desc.sampler = var_desc.sampler;
            rr_var_desc.stages = resource->desc.allowed_stages;
            layout_desc.bindings.push_back(rr_var_desc);
        }
    }
    
    

    VkRenderResourceInfo info;
    info.descritor_set_layout_desc = layout_desc;
    info.layout = buffer_manager.create_descriptor_set_layout(layout_desc, resource->desc.name);
    resources_info.insert({resource, info});
    
    return info.layout;
}

void vk::PipelineManager::push_constants(const RBCommandList& cmd, const void* data, size_t size)
{
    PROFILE("vk::PipelineManager::push_constants");

    const PipelineLayoutDesc& pipeline_layout_desc = get_pipeline_layout_desc(current_pipeline_layout);
    
    uint32_t stage_bits = 0;
    
    for (auto& stage : pipeline_layout_desc.push_constants)
    {
        if ((stage.stages & ShaderStage::fragment) != ShaderStage::none)
            stage_bits |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if ((stage.stages & ShaderStage::vertex) != ShaderStage::none)
            stage_bits |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    
    auto vk_pipeline_layout = current_pipeline_layout.as<VkPipelineLayout>();
    
    {
        PROFILE("vkCmdPushConstants");
        vkCmdPushConstants(
            cmd,
            vk_pipeline_layout,
            VkShaderStageFlagBits(stage_bits), 
            0, 
            size,
            data
        );
    }
    
}

void vk::PipelineManager::bind_descriptor_set(RBCommandList cmd_list, int set_index, RBDescriptorSet rb_descriptors,
                                              Name debug_name)
{
    
    PROFILE("bind_descriptor_set");
    
    VkDescriptorSet vk_set = rb_descriptors.as<VkDescriptorSet>();
    
    if (vk_set == current_descriptor_set)
        return;
    
    LogVkPipelineManager.Log("bind_descriptor_set. cmd: %p, descriptor_set: %p, pipeline_layout: %p (%s)",
        cmd_list, rb_descriptors, current_pipeline_layout, debug_name.to_string().c_str());
    
    current_descriptor_set = vk_set;
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    
    auto vk_pipeline_layout = current_pipeline_layout.as<VkPipelineLayout>();
    
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
    
    current_pipeline_layout = pipeline_object->get_layout();
}

void vk::PipelineManager::invalidate_pipeline_layout()
{
    current_pipeline_layout = RBPipelineLayout();
}

void vk::PipelineManager::update_buffers()
{
    for (const auto& [resource_and_id, instances] : unique_resource_instances)
    {
        auto [resource, unique_id] = resource_and_id;
        for (std::shared_ptr<VkRenderResourceInstance> instance : instances)
        {
            VkRenderResourceInstance* instance_raw = instance.get();
            const auto info_it = resources_info.find(resource);
            
            checkf(info_it != resources_info.end(), "Resource (%s) not found",
                resource->desc.name.to_string().c_str() );
            
            auto info = info_it->second;

            if (resource_instance_data.contains(instance.get()))
                continue;
        
            if (!resource_instance_data.contains(instance_raw))
                resource_instance_data.insert({instance_raw, {}});
            auto& inst = resource_instance_data.at(instance_raw);

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
