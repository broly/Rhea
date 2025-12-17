#pragma once
#include <vector>
#include <GLFW/glfw3.h>

#include "vk_context.h"
#include "vk_pipeline.h"
#include "framework/camera.h"
#include "render/render_backend.h"

class VkRenderBackend final : public RenderBackend
{
public:
    virtual void init(void* window) override;
    virtual void draw_frame(const Camera& camera) override;
    
    
private: // Initialization section
    void create_instance();
    void match_queue_families();
    void create_device();
    void create_frame_sync_objects();
    
private: // Re-/Initialization section
    void create_render_pass();
    void create_framebuffers();
    void create_frame_resources();
    
    void create_swapchain();
    void create_pipeline();
    void create_command_pool();
    
    void record_command_buffers(uint32_t image_index);
    void create_render_finished_semaphores();

private: // Special section
    void cleanup_swapchain();
    void recreate_swapchain();
    void create_images_context();

    void create_depth_resources();
    
private: // Camera section
    // void create_camera_ubo();
    void update_camera_ubo(vk::FrameContext& FrameContext, const Camera& camera);
    void create_descriptor_set_layout();
    void create_descriptor_pool();
    void allocate_descriptor_sets();
    void update_descriptor_sets();
    

    vk::Context context = {};
    vk::InstanceContext instance_context = {};
    vk::SwapchainContext swapchain_context = {};
    vk::CommandContext command_context = {};
    vk::FrameScheduleContext frame_schedule_context = {};
    vk::PipelineContext pipeline_context = {};
    vk::DescriptorContext descriptor_context = {};
    std::vector<vk::ImageContext> images;
    
    
    std::unique_ptr<VkPipelineObject> pipeline;
    
    bool framebuffer_resized = false;
    
    GLFWwindow* window = nullptr;
};
