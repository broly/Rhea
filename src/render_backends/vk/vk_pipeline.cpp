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
import :enums_adapters;
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

    // if (pipeline_layout != nullptr)
    //     vkDestroyPipelineLayout(instance.device, pipeline_layout, nullptr);
}


void VkPipelineObject::prepare()
{

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
    
    for (auto& stage : pipeline_desc->stages)
    {
        checkf(stage.compiled_shader.has_value(), "shader not compiled!");
        VkShader stage_shader(instance.device, *stage.compiled_shader);
        reflect_shader(stage_shader, stage.stage);
        shaders.push_back(std::move(stage_shader));
    }    

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
            auto stage_reflection = pipeline_reflection.at(ShaderStage::vertex);
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
            shaders.at(stage_index).get_module(),
            "main"
        };
        vk_stages.push_back(vk_stage);
        
        stage_index++;
    }


    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    input_assembly.topology = vk::to_vk_primitive_topology(pipeline_desc->topology); // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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
    depth_ci.minDepthBounds = 0.0f;
    depth_ci.maxDepthBounds = 1.0f;
    
    pci.stageCount = vk_stages.size();
    pci.pStages = vk_stages.data();
    pci.pVertexInputState = &vertex_input;
    pci.pInputAssemblyState = &input_assembly;
    pci.pViewportState = &viewport_state;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &blend;
    checkf(pipeline_desc->layout.pipeline_layout != 0, "Pipeline layout is null");
    pci.layout = pipeline_desc->layout.pipeline_layout;
    pci.renderPass = render_pass;
    pci.subpass = 0;
    pci.pDepthStencilState = &depth_ci;
    pci.pDynamicState = &dynamic_ci;
    
    if (!pipeline_desc->no_color_attachments) {
        pci.pColorBlendState = &blend;
    } else {
        pci.pColorBlendState = nullptr;  // Depth-only
    }

    VK_CHECK(vkCreateGraphicsPipelines(
        instance.device,
        VK_NULL_HANDLE,
        1,
        &pci,
        nullptr,
        &vk_pipeline));
    
    LogVkPipeline.Log<DisplayFn>("Created graphics pipeline %p (pass: %s, pipeline_layout: %p):",
        vk_pipeline, pipeline_desc->pass_name.to_string().c_str(), pipeline_desc->layout.pipeline_layout);
    for (auto& stage : pipeline_desc->stages)
    {
        LogVkPipeline.Log<DisplayFn>(" * stage '%s', shader: '%s'", 
            reflect::enum_name(stage.stage).to_string().c_str(),
            stage.compiled_shader->c_str());
    }
    
    return vk_pipeline;
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
