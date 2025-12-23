#include "vk_pipeline.h"

#include "vk_helpers.h"
#include "vk_macro.h"
#include "vk_render_backend.h"
#include "vk_shader.h"
#include "render/graphics_pipeline.h"

class VkRenderBackend;

VkPipelineObject::VkPipelineObject(
    vk::InstanceContext& instance,
    vk::SwapchainContext& swapchain,
    const GraphicsPipelineDesc& desc,
    VkRenderBackend& backend)
        : instance_context(instance)
        , swapchain_context(swapchain)
{
    std::vector<VkDescriptorSetLayout> vk_layouts;

    for (RBDescriptorSetLayout h : desc.layout.sets)
    {
        vk_layouts.push_back(
            backend.get_vk_descriptor_set_layout(h).vk_layout
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
    
    
    
    VkShader vert(instance.device, desc.vertex_shader);
    VkShader frag(instance.device, desc.fragment_shader);

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
    
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attrs{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, tex_coord) };

    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = attrs.size();
    vertex_input.pVertexAttributeDescriptions = attrs.data();


    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{
        0, 0,
        (float)swapchain.extent.width,
        (float)swapchain.extent.height,
        0.0f, 1.0f
    };

    VkRect2D scissor{{0,0}, swapchain.extent};

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
    pci.renderPass = swapchain.render_pass;
    pci.subpass = 0;
    pci.pDepthStencilState = &depth_ci;

    VK_CHECK(vkCreateGraphicsPipelines(
        instance.device,
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
