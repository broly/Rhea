#include "vk_pipeline.h"

#include "vk_macro.h"
#include "vk_shader.h"


VkPipelineObject::VkPipelineObject(
    vk::InstanceContext& in_instance_context, 
    vk::SwapchainContext& in_swapchain_context,
    const VkDescriptorSetLayout camera_set_layout)
    : instance_context(in_instance_context)
    , swapchain_context(in_swapchain_context)
{
    VkPipelineLayoutCreateInfo plci{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &camera_set_layout;

    VK_CHECK(vkCreatePipelineLayout(
        instance_context.device,
        &plci,
        nullptr,
        &pipeline_layout));
    
    
    
    
    VkShader vert(in_instance_context.device, "shaders/cube.vert.spv");
    VkShader frag(in_instance_context.device, "shaders/cube.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};

    stages[0] = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0,
        VK_SHADER_STAGE_VERTEX_BIT,
        vert.get_module(),
        "main"
    };

    stages[1] = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        frag.get_module(),
        "main"
    };

    // fixed-function (minimal)
    VkPipelineVertexInputStateCreateInfo vertex_input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{
        0, 0,
        (float)in_swapchain_context.extent.width,
        (float)in_swapchain_context.extent.height,
        0.0f, 1.0f
    };

    VkRect2D scissor{{0,0}, in_swapchain_context.extent};

    VkPipelineViewportStateCreateInfo viewport_state{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;

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

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    blend.attachmentCount = 1;
    blend.pAttachments = &color;
    
    VkPipelineDepthStencilStateCreateInfo depth_ci{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depth_ci.depthTestEnable = VK_TRUE;
    depth_ci.depthWriteEnable = VK_TRUE;
    depth_ci.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_ci.depthBoundsTestEnable = VK_FALSE;
    depth_ci.stencilTestEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pci{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertex_input;
    pci.pInputAssemblyState = &input_assembly;
    pci.pViewportState = &viewport_state;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &blend;
    pci.layout = pipeline_layout;
    pci.renderPass = in_swapchain_context.render_pass;
    pci.subpass = 0;
    pci.pDepthStencilState = &depth_ci;

    VK_CHECK(vkCreateGraphicsPipelines(
        in_instance_context.device,
        VK_NULL_HANDLE,
        1,
        &pci,
        nullptr,
        &pipeline_));

    
    shaders.push_back(std::move(vert));
    shaders.push_back(std::move(frag));
}

VkPipelineObject::~VkPipelineObject()
{
    if (pipeline_ != nullptr)
        vkDestroyPipeline(instance_context.device, pipeline_, nullptr);
    
    if (pipeline_layout != nullptr)
        vkDestroyPipelineLayout(instance_context.device, pipeline_layout, nullptr);
}
