#pragma once
#include <vulkan/vulkan_core.h>

#include "vk_context.h"
#include "vk_shader.h"

class VkPipelineObject
{
public:
    VkPipelineObject(VkContext& ctx);
    ~VkPipelineObject() = default;

    VkPipeline pipeline() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

private:
    VkContext& context;

    VkPipeline pipeline_;
    VkPipelineLayout layout_;
    
    std::vector<VkShader> shaders;
};
