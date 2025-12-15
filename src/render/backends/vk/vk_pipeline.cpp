#include "vk_pipeline.h"

#include "vk_macro.h"
#include "vk_shader.h"


VkPipelineObject::VkPipelineObject(VkContext& ctx)
    : context(ctx)
{
    VkShader vert(ctx.device, "shaders/triangle.vert.spv");
    VkShader frag(ctx.device, "shaders/triangle.frag.spv");

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


    VkPipelineVertexInputStateCreateInfo vertex_input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{
        0, 0,
        (float)ctx.extent.width,
        (float)ctx.extent.height,
        0.0f, 1.0f
    };

    VkRect2D scissor{{0,0}, ctx.extent};

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
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
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

    VkPipelineLayoutCreateInfo layout_ci{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };

    VK_CHECK(vkCreatePipelineLayout(
        ctx.device, &layout_ci, nullptr, &layout_));

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
    pci.layout = layout_;
    pci.renderPass = ctx.render_pass;
    pci.subpass = 0;

    VK_CHECK(vkCreateGraphicsPipelines(
        ctx.device,
        VK_NULL_HANDLE,
        1,
        &pci,
        nullptr,
        &pipeline_));

    
    shaders.push_back(std::move(vert));
    shaders.push_back(std::move(frag));
}
