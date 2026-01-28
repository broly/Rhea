module vk:pipeline;
import render;
import :helpers;
import rhmath;
import <vector>;
import <vulkan/vulkan_core.h>;
import <cassert>;
import spirv_reflect;
import :reflection;
import :render_resource;
import :log;
#include "vk_macro.h"
import reflect;
import <bit>;
import <set>;
#include "common/assertion_macros.h"

class VkRenderBackend;

VkPipelineObject::VkPipelineObject(
    vk::Instance& in_instance,
    vk::SwapchainControl& in_swapchain,
    vk::BufferManager& in_buffer_manager,
    const GraphicsPipelineDesc& desc)
        : pipeline_desc(desc)
        , instance(in_instance)
        , swapchain(in_swapchain)
        , buffer_manager(in_buffer_manager)
{
    debug_name = desc.pass_name;
    permutation_value = desc.permutation_value;
    prepare();
}

VkPipelineObject::~VkPipelineObject()
{
    if (vk_pipeline != nullptr)
        vkDestroyPipeline(instance.device, vk_pipeline, nullptr);

    if (pipeline_layout != nullptr)
        vkDestroyPipelineLayout(instance.device, pipeline_layout, nullptr);
}


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


void VkPipelineObject::prepare()
{

    GraphicsPipelineDesc& desc = *pipeline_desc;
    
    
    for (auto& stage : desc.stages)
    {
        checkf(stage.compiled_shader.has_value(), "shader not compiled!");
        VkShader stage_shader(instance.device, *stage.compiled_shader);
        reflect_shader(stage_shader, stage.stage);
        shaders.push_back(std::move(stage_shader));
    }
    
    std::vector<VkDescriptorSetLayout> vk_layouts;
    const auto& reflection = get_reflection();
    
    for (auto& resource_info : desc.layout.resources)
    {
        RenderResource* resource = resource_info.resource;
        checkf(resource, "NULL resource detected");
        
        
        std::vector<DescriptorBinding> bindings;
        std::set<size_t> handled_variable_indices;
        DescriptorSetLayoutDesc layout_desc;
        
        std::set<int> occupied_variables;
        for (const auto& [stage, refl] : reflection)
        {
            if ((resource->desc.stages & stage) != ShaderStage::none)
            {
                checkf(resource_info.resource_variable_bindings.size() == resource->desc.variables.size(),
                    "Variables num is different with variable bindings num");
                for (int variable_index = 0; variable_index < resource_info.resource->desc.variables.size(); ++variable_index)
                {
                    auto& variable = resource->desc.variables[variable_index];
                    uint32_t variable_binding = resource_info.resource_variable_bindings[variable_index];
                    
                    auto it = refl.bindings.find(variable.name);
                    if (it == refl.bindings.end())
                        continue;
                    occupied_variables.insert(variable_index);
                    
                    checkf(variable_binding == it->second.binding,
                        "Variable binding is different: queried: %i and shader: %i", variable_binding, it->second.binding);
                    checkf(resource_info.set == it->second.set,
                        "Variable set is different: queried: %i and shader: %i", resource_info.set, it->second.set);
                    // checkf(variable.size == it->second.size)
                    
                    if (handled_variable_indices.contains(variable_index))
                        continue;
                    
                    
                    DescriptorBinding binding{};
                    binding.name          = variable.name;
                    binding.binding_index = variable_binding;
                    binding.count         = it->second.count; // todo!
                    binding.size          = variable.size;
                    binding.stages        = resource->desc.stages;
                    binding.type          = TEMP_CONVERT_DESCRIPTOR_TYPE_CRUTCH(it->second.descriptor_type);
                    bindings.push_back(binding);
                    handled_variable_indices.insert(variable_index);
                }
            }
        }
        if (occupied_variables.size() != resource->desc.variables.size())
        {
            for (int variable_index = 0; variable_index < resource->desc.variables.size(); ++variable_index)
            {
                checkf(occupied_variables.contains(variable_index), "Variable %s declared, but not used in any shader",
                    resource->desc.variables[variable_index].name.to_string().c_str());
            }
        }
        
        layout_desc.bindings = bindings;
        layout_desc.set_index = resource_info.set;
        layout_desc.debug_name = resource->desc.name;
            
        VkRenderResourcePipelineInfo info{};
        info.descritor_set_layout_desc = layout_desc;
        info.layout = buffer_manager.create_descriptor_set_layout(layout_desc);
            
        resources_pipeline_info.insert({(VkRenderResource*)resource, std::move(info)});
        
        auto h = resources_pipeline_info[(VkRenderResource*)resource].layout;
        vk_layouts.push_back(
            buffer_manager.get_vk_descriptor_set_layout(h).vk_layout
        );
    }

    VkPipelineLayoutCreateInfo plci{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    plci.setLayoutCount = uint32_t(vk_layouts.size());
    plci.pSetLayouts = vk_layouts.data();

    // push constants
    plci.pushConstantRangeCount = desc.layout.push_constants.size();
    std::vector<VkPushConstantRange> push_constants = vk::to_vk_ranges(desc.layout.push_constants);
    plci.pPushConstantRanges = push_constants.data();

    vkCreatePipelineLayout(instance.device, &plci, nullptr, &pipeline_layout);
}


static constexpr std::array<VkShaderStageFlagBits, MAX_STAGES> get_vk_stages()
{
    std::array<VkShaderStageFlagBits, MAX_STAGES> stages_vk_bits;
    stages_vk_bits[ShaderStage_index(ShaderStage::vertex)] = VK_SHADER_STAGE_VERTEX_BIT;
    stages_vk_bits[ShaderStage_index(ShaderStage::fragment)] = VK_SHADER_STAGE_FRAGMENT_BIT;
    return stages_vk_bits;
}

constexpr std::array<VkShaderStageFlagBits, MAX_STAGES> STAGES_VK_BITS = get_vk_stages();

VkCullModeFlags conv_cull_mode(CullMode cull_mode)
{
    switch (cull_mode) {
    case CullMode::none:
        return VK_CULL_MODE_NONE;
    case CullMode::front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::back:
        return VK_CULL_MODE_BACK_BIT;
    case CullMode::both:
        return VK_CULL_MODE_FRONT_AND_BACK;
    }
    unreachable("error");
}


VkCompareOp conv_compare_op(CompareOp compare_op)
{
    switch (compare_op) {
    case CompareOp::never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareOp::less_or_equal:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::greater:
        return VK_COMPARE_OP_GREATER;
    case CompareOp::not_equal:
        return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::greater_or_equal:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::always:
        return VK_COMPARE_OP_ALWAYS;
    }
    unreachable("error");
}

VkFrontFace conv_front_face(FrontFace front_face)
{
    switch (front_face) {
    case FrontFace::CW:
        return VK_FRONT_FACE_CLOCKWISE;
    case FrontFace::CCW:
        return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
    unreachable("error");
}

VkPipeline VkPipelineObject::get_or_create_pipeline(VkRenderPass render_pass)
{
    if (vk_pipeline != nullptr)
        return vk_pipeline;

    std::vector<VkPipelineShaderStageCreateInfo> vk_stages;
    
    
    VkGraphicsPipelineCreateInfo pci{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
    
    
    std::vector<VkVertexInputBindingDescription> vertex_input_bindings;
    std::vector<VkVertexInputAttributeDescription> vk_attrs;
    VkPipelineVertexInputStateCreateInfo vertex_input = VkPipelineVertexInputStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
            
    for (const auto& layout : pipeline_desc->layout.vertex_layout.layouts)
    {
        VkVertexInputBindingDescription vertex_input_binding;
        vertex_input_binding.stride = layout.stride;
        vertex_input_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        vertex_input_binding.binding = layout.binding_index;
                
        for (const auto& attribute_info : layout.attributes)
        {
            auto stage_reflection = pipeline_reflection[ShaderStage::vertex];
            assert(stage_reflection.input_variables.contains(attribute_info.variable_name));

            ReflectedInterfaceVariable& reflection_info = stage_reflection.input_variables[attribute_info.variable_name];
                    
            checkf(reflection_info.location == attribute_info.location,
                "Attribute %s location mismatch %i and %i",
                attribute_info.variable_name, attribute_info.location, reflection_info.location);
                
            VkVertexInputAttributeDescription attr;
            attr.location = attribute_info.location;
            attr.offset = attribute_info.offset;
            attr.format = reflection_info.format;
            attr.binding = layout.binding_index;
            vk_attrs.push_back(attr);
        }
        vertex_input_bindings.push_back(vertex_input_binding);
    }

    vertex_input.vertexBindingDescriptionCount = vertex_input_bindings.size();
    vertex_input.pVertexBindingDescriptions = vertex_input_bindings.data();
    vertex_input.vertexAttributeDescriptionCount = vk_attrs.size();
    vertex_input.pVertexAttributeDescriptions = vk_attrs.data();
    
    for (uint32_t stage_index = 0; auto& stage : pipeline_desc->stages)
    {
        VkPipelineShaderStageCreateInfo vk_stage {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            STAGES_VK_BITS[ShaderStage_index(stage.stage)],
            shaders[stage_index].get_module(),
            "main"
        };
        vk_stages.push_back(vk_stage);
        
        stage_index++;
    }


    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_ci{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    dynamic_ci.dynamicStateCount = 2;
    dynamic_ci.pDynamicStates = dynamic_states;

    VkPipelineViewportStateCreateInfo viewport_state{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = nullptr;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = nullptr;
    
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = conv_cull_mode(pipeline_desc->cull_mode); // VK_CULL_MODE_BACK_BIT;
    raster.frontFace = conv_front_face(pipeline_desc->front_face); // VK_FRONT_FACE_CLOCKWISE;
    if (pipeline_desc->depth_bias.enable)
    {
        raster.depthBiasConstantFactor = pipeline_desc->depth_bias.constant_factor;
        raster.depthBiasClamp = pipeline_desc->depth_bias.clamp;
        raster.depthBiasSlopeFactor = pipeline_desc->depth_bias.slope_factor;
    }
    
    
    
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color{};
    color.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    
    if (pipeline_desc->is_translucent)
    {
        color.blendEnable = VK_TRUE; 
        color.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; 
        color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; 
        color.colorBlendOp = VK_BLEND_OP_ADD; 
        color.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; 
        color.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; 
        color.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    
    

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    blend.attachmentCount = 1;
    blend.pAttachments = &color;

    VkPipelineDepthStencilStateCreateInfo depth_ci{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depth_ci.depthTestEnable = pipeline_desc->depth_test ? VK_TRUE : VK_FALSE;
    depth_ci.depthWriteEnable = pipeline_desc->depth_write ? VK_TRUE : VK_FALSE;
    depth_ci.depthCompareOp = conv_compare_op(pipeline_desc->compare_op);
    depth_ci.depthBoundsTestEnable = VK_FALSE;
    depth_ci.stencilTestEnable = VK_FALSE;

    pci.stageCount = vk_stages.size();
    pci.pStages = vk_stages.data();
    pci.pVertexInputState = &vertex_input;
    pci.pInputAssemblyState = &input_assembly;
    pci.pViewportState = &viewport_state;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &blend;
    pci.layout = pipeline_layout;
    pci.renderPass = render_pass;
    pci.subpass = 0;
    pci.pDepthStencilState = &depth_ci;
    pci.pDynamicState = &dynamic_ci;

    VK_CHECK(vkCreateGraphicsPipelines(
        instance.device,
        VK_NULL_HANDLE,
        1,
        &pci,
        nullptr,
        &vk_pipeline));
    
    LogVkPipeline.Log<DisplayFn>("Created graphics pipeline %p (pass: %s):",
        vk_pipeline, pipeline_desc->pass_name.to_string().c_str());
    for (auto& stage : pipeline_desc->stages)
    {
        LogVkPipeline.Log<DisplayFn>(" * stage '%s', shader: '%s'", 
            reflect::enum_name(stage.stage).to_string().c_str(),
            stage.compiled_shader->c_str());
    }
    
    return vk_pipeline;
}

RenderResourceInstance* VkPipelineObject::query_unique_resource_instance(RenderResource* resource)
{
    if (unique_resource_instances.contains((VkRenderResource*)resource))
        return unique_resource_instances.at((VkRenderResource*)resource);
    
    auto resource_instance = create_resource_instance(resource);
    
    update_buffers();
    
    unique_resource_instances.insert({(VkRenderResource*)resource, (VkRenderResourceInstance*)resource_instance});
    return resource_instance;
}

RenderResourceInstance* VkPipelineObject::create_resource_instance(RenderResource* resource)
{
    std::unique_ptr<VkRenderResourceInstance> instance = std::make_unique<VkRenderResourceInstance>(
        buffer_manager, *(VkRenderResource*)resource, resource->desc.usage_type);
    
    instances.push_back(std::move(instance));
    
    update_buffers();
    
    return instances.back().get();
}

void VkPipelineObject::update_buffers()
{
    for (auto& instance : instances)
    {
        auto resource = &instance->resource;  // todo make getter
        const auto& info = resources_pipeline_info.at((VkRenderResource*)&instance->resource);

        if (instance_pipeline_data.contains(instance.get()))
            continue;
        
        if (!instance_pipeline_data.contains(instance.get()))
            instance_pipeline_data.insert({instance.get(), {}});
        auto& inst = instance_pipeline_data.at(instance.get());

        inst.sets_per_frame =
            buffer_manager.allocate_descriptor_sets_for_layout(
                info.layout, resource->desc.usage_type);

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
                    buffer_manager.create_uniform_buffer(binding.size, resource->desc.usage_type);

                buffer_manager.bind_buffer_to_descriptor(
                    inst.sets_per_frame[frame],
                    binding.binding_index,
                    buffer,
                    frame);

                buffers_per_frame.push_back(buffer);
            }
        }
    }
}

DescriptorType to_descriptor_type(SpvReflectDescriptorType type)
{
    switch (type)
    {
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
        return DescriptorType::Sampler;
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return DescriptorType::CombinedImageSampler;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return DescriptorType::SampledImage;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        break;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        break;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        break;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return DescriptorType::UniformBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return DescriptorType::StorageBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        break;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        break;
    case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        break;
    case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        break;
    }
    throw std::runtime_error("Invalid descriptor type");
}


void VkPipelineObject::reflect_shader(const VkShader& shader, ShaderStage stage)
{
    SpirvReflection reflection(shader.get_spirv());
    
    pipeline_reflection.insert({stage, 
        PipelineReflection(
            shader.get_shader_name(),
            reflection.get_input_variables(), 
            reflection.get_bindings())});
}
