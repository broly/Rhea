module vk:pipeline_manager;
import :helpers;
import <vector>;
import :log;
import :pipeline_graphics;
import :pipeline_compute;
import :pipeline_raytrace;
import <vulkan/vulkan_core.h>;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"
#include "render_backends/vk/vk_macro.h"

DEFINE_LOGGER(LogVkPipelineManager, Warning);


PipelineObject* vk::PipelineManager::create_graphics_pipeline(const PipelineCreateDesc_Graphics& desc, RBPipelineLayout pipeline_layout)
{
    std::unique_ptr<VkPipelineObject_Graphics> pipeline = std::make_unique<VkPipelineObject_Graphics>(instance, swapchain, buffer_manager,debug_object_tracker,  pipeline_layout, desc);
    PipelineObject* result = pipeline.get();
    pending_pipelines.push_back(std::move(pipeline));
    return result;
}

PipelineObject* vk::PipelineManager::create_compute_pipeline(const PipelineCreateDesc_Compute& desc, RBPipelineLayout pipeline_layout)
{
    std::unique_ptr<VkPipelineObject_Compute> pipeline = std::make_unique<VkPipelineObject_Compute>(instance, swapchain, buffer_manager,debug_object_tracker,  pipeline_layout, desc);
    PipelineObject* result = pipeline.get();
    pending_pipelines.push_back(std::move(pipeline));
    return result;
}

PipelineObject* vk::PipelineManager::create_raytrace_pipeline(const PipelineCreateDesc_RayTrace& desc,
    RBPipelineLayout pipeline_layout)
{
    std::unique_ptr<VkPipelineObject_RayTrace> pipeline = std::make_unique<VkPipelineObject_RayTrace>(instance, swapchain, buffer_manager, debug_object_tracker, pipeline_layout, desc);
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
        uint32_t resource_set = resource_info.resource->desc.set_index;
        RBDescriptorSetLayout layout_id = get_or_create_resource_descriptor_set_layout(
            (VkRenderResource*)resource_info.resource);
        vk_layouts[resource_set] = buffer_manager.get_vk_descriptor_set_layout(layout_id).vk_layout;
        
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
    debug_object_tracker.register_object(pipeline_layout, desc.pass);
    
    
    
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
    
    instance_data[pipeline_layout] = {desc, vk_layouts};
    
    RBPipelineLayout result {pipeline_layout};
    
    return result;
}

void vk::PipelineManager::destroy_pipeline(PipelineObject* pipeline)
{
    auto as_vk = static_cast<VkPipelineObject*>(pipeline);
    if (as_vk->has_pipeline_handle())
    {
        pipelines.erase(as_vk->get_pipeline_handle());
        return;
    }
    for (auto it = pending_pipelines.begin(); it != pending_pipelines.end(); ++it)
    {
        if (it->get() == pipeline)
        {
            pending_pipelines.erase(it);
            break;
        }
    }
    
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
        stage_bits |= vk::to_vk_shader_stage_flags(stage.stages);
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

    // Bind point comes from the currently-bound pipeline object. If no
    // pipeline is currently bound there is nothing meaningful to bind a
    // descriptor set to anyway, so assert.
    checkf(current_pipeline_object != nullptr,
        "bind_descriptor_set called with no pipeline bound");

    const VkPipelineBindPoint bind_point = current_pipeline_object->get_bind_point();

    // Per-(bind_point, set_index) early-out. Vulkan tracks separate bind
    // tables per bind point: binding to graphics doesn't affect compute,
    // so the cache key must include the bind point.
    const BindKey key{ bind_point, uint32_t(set_index) };
    auto it = current_descriptor_sets.find(key);
    if (it != current_descriptor_sets.end() && it->second == vk_set)
        return;

    LogVkPipelineManager.Log("bind_descriptor_set. cmd: %p, descriptor_set: %p, pipeline_layout: %p (%s)",
        cmd_list, rb_descriptors, current_pipeline_layout, debug_name.to_string().c_str());

    current_descriptor_sets[key] = vk_set;

    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();

    auto vk_pipeline_layout = current_pipeline_layout.as<VkPipelineLayout>();

    auto& inst_data = instance_data[vk_pipeline_layout];
    auto& desc_set_layout = inst_data.desc_set_layouts[set_index];

    checkf(desc_set_layout != *empty_descriptor_set,
        "Could not bind descriptor set, because it is not provided for this pipeline layout. "
        "Check this callstack to detect resource name and check the configs");

    {
        PROFILE("vkCmdBindDescriptorSets");
        vkCmdBindDescriptorSets(
            cmd,
            bind_point,
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
    
    vkCmdBindPipeline(cmd, vk_pipeline_object->get_bind_point(), pipeline_vk);

    // ----------------------------------------------------------------
    // Pipeline layout compatibility — descriptor set bind cache.
    //
    // Vulkan keeps descriptor sets attached to a bind point only as long
    // as subsequent pipelines on that bind point use a *compatible*
    // pipeline layout. Two layouts are compatible for a given set N only
    // if they share the same push-constant ranges AND identical
    // descriptor set layouts for sets 0..N. As soon as we bind a
    // pipeline whose layout is NOT bit-equal to the previously bound
    // layout, we must conservatively assume the bind table for this bind
    // point has been invalidated, and force every subsequent
    // bind_descriptor_set to actually emit a vkCmdBindDescriptorSets.
    //
    // We previously cached "current_descriptor_set" globally and so this
    // worked by accident — every subsequent set had a different vk_set
    // handle from the cached one and therefore still got rebound. Once
    // we made the cache per (bind_point, set_index), the cache started
    // surviving pipeline-layout changes, leaving sets bound under the
    // OLD layout when the NEW pipeline expected the NEW layout. The
    // validator catches this as VUID-vkCmdDispatch-None-08600 with
    // messages like "all sets 0 to N are not compatible with the
    // pipeline layout bound with vkCmdBindDescriptorSets", or "set N is
    // out of bounds for the number of sets bound".
    //
    // The fix: when the new pipeline's layout differs from the
    // previously bound layout for this same bind point, drop all cached
    // entries for this bind point. Same-layout rebinds (e.g. successive
    // NN_ENC compute passes that share NN_ENC layout but use different
    // descriptor sets per pass_idx) keep the cache and avoid redundant
    // vkCmdBindDescriptorSets calls.
    // ----------------------------------------------------------------
    const VkPipelineBindPoint bind_point = vk_pipeline_object->get_bind_point();
    const RBPipelineLayout new_layout = pipeline_object->get_layout();

    auto it_prev_layout = last_layout_per_bind_point.find(bind_point);
    const bool layout_changed =
        it_prev_layout == last_layout_per_bind_point.end() ||
        it_prev_layout->second.as<VkPipelineLayout>() != new_layout.as<VkPipelineLayout>();

    if (layout_changed)
    {
        // Drop only the bind-table cache for this bind point. Other bind
        // points (graphics vs compute vs raytrace) have independent
        // tables and are unaffected.
        for (auto it = current_descriptor_sets.begin(); it != current_descriptor_sets.end(); )
        {
            if (it->first.bind_point == bind_point)
                it = current_descriptor_sets.erase(it);
            else
                ++it;
        }
        last_layout_per_bind_point[bind_point] = new_layout;
    }

    current_pipeline_layout = new_layout;
    current_pipeline_object = vk_pipeline_object;
}

void vk::PipelineManager::invalidate_pipeline_layout()
{
    current_pipeline_layout = RBPipelineLayout();
    current_pipeline_object = nullptr;

    // Forget cached descriptor-set bindings as well — anything we
    // recorded as "currently bound" was bound on a previous command
    // buffer or render pass and is no longer in effect.
    current_descriptor_sets.clear();
    last_layout_per_bind_point.clear();
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

                if (binding.parameter.type != MaterialParamType::uniform && binding.parameter.type != MaterialParamType::ssbo)
                    continue;

                VkDescriptorType descriptor_type = to_vk_descriptor_type(binding.parameter.type);

                auto& buffers_per_frame = inst.buffers[binding_index];

                for (size_t frame = 0; frame < inst.sets_per_frame.size(); ++frame)
                {
                    RBBufferHandle buffer = binding.parameter.type == MaterialParamType::uniform ?
                        buffer_manager.create_uniform_buffer(*binding.parameter.ubo_size, resource->desc.usage) :
                        buffer_manager.create_storage_buffer(*binding.parameter.initial_buffer_size, resource->desc.usage, /* host visible = */true);

                    buffer_manager.bind_buffer_to_descriptor(
                        inst.sets_per_frame[frame],
                        *binding.binding_index,
                        buffer,
                        frame,
                        descriptor_type);

                    buffers_per_frame.push_back(buffer);
                }
            }
        }
    }
}