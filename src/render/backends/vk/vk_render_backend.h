#pragma once
#include <vector>
#include <GLFW/glfw3.h>

#include "vk_context.h"
#include "vk_pipeline.h"
#include "render/render_backend.h"

class VkRenderBackend final : public RenderBackend
{
public:
    virtual void init(void* window) override;
    virtual void draw_frame() override;
    void create_render_pass(VkSurfaceFormatKHR surfaceFormat);
    void create_framebuffers();
    
    VkContext context = {};
    std::unique_ptr<VkPipelineObject> pipeline;
};
