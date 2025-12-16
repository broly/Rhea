#pragma once
#include <vector>
#include <GLFW/glfw3.h>

#include "vk_context.h"
#include "vk_pipeline.h"
#include "render/render_backend.h"

class VkRenderBackend final : public RenderBackend
{
public:
    void record_command_buffers();
    virtual void init(void* window) override;
    virtual void draw_frame() override;
    
    void create_instance();
    void match_queue_families();
    void create_device();

    void recreate_swapchain();
    void create_render_pass();
    void create_framebuffers();
    void create_swapchain();
    void create_pipeline();
    void create_command_pool();
    
    void create_frame_sync_objects();
    void create_image_sync_objects();
    
    void cleanup_swapchain();
    
    
    VkContext context = {};
    std::unique_ptr<VkPipelineObject> pipeline;
    
    bool framebuffer_resized = false;
    
    GLFWwindow* window = nullptr;
};
