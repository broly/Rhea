#pragma once
#include <vulkan/vulkan_core.h>

#include "vk_context.h"
#include "vk_shader.h"
#include "render/graphics_pipeline.h"
#include "render/pipeline_object.h"

struct GraphicsPipelineDesc;

class VkPipelineObject : public PipelineObject
{
public:
    VkPipelineObject(
        const GraphicsPipelineDesc& desc,
        class VkRenderBackend& in_backend);
    ~VkPipelineObject();

    RBPipelineHandle get_pipeline_handle() const override
    {
        assert(pipeline_ != VK_NULL_HANDLE); 
        return pipeline_;
    }
    
    VkPipelineLayout get_pipeline_layout() const
    {
        assert(pipeline_ != VK_NULL_HANDLE); 
        return pipeline_layout;
    }
    VkPipeline get_or_create_pipeline(VkRenderPass render_pass);
    
    GraphicsPipelineDesc pipeline_desc;

private:
    VkRenderBackend& backend;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    
    std::optional<VkShader> vert;
    std::optional<VkShader> frag;
    
};
