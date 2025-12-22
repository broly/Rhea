#pragma once
#include <map>
#include <span>
#include <vector>
#include <GLFW/glfw3.h>

#include "vk_context.h"
#include "vk_pipeline.h"
#include "framework/camera.h"
#include "render/render_backend.h"

struct DescriptorSetLayoutData
{
    VkDescriptorSetLayout vk_layout;
    uint32_t set_index;
};

class VkRenderBackend final : public RenderBackend
{
public:
    void init(RBWindowHandle window);
    // virtual void draw_frame(const Camera& camera) override;
    
    RBCommandList begin_commands(RBFrameHandle frame_handle) override;
    void end_commands(RBCommandList cmd_list) override;
    void begin_render_pass(RBCommandList cmd_list, RBFramebufferId framebuffer_index) override;
    void end_render_pass(RBCommandList cmd_list) override;
    void bind_pipeline(RBCommandList cmd_list, RBPipelineHandle pipeline_handle) override;
    void draw(RBCommandList cmd_list, uint32_t vertex_count) override;
    void update_camera_ubo(RBFrameHandle frame_handle, const Camera& camera) override;
    RBFramebufferId acquire_next_image(RBFrameHandle frame_handle) override;
    void submit_frame(RBFrameHandle frame_handle, RBCommandList cmd_list, RBFramebufferId framebuffer_id) override;
    RBPipelineHandle create_pipeline(GraphicsPipelineDesc desc) override;
    DescriptorSetLayoutData get_vk_descriptor_set_layout(const RBDescriptorSetLayout& rb_handle);
    virtual RBDescriptorSet get_descriptor_set(RBDescriptorSetLayout rb_descriptor_set_layout, DescriptorPoolType pool_type) override;
    void update_descriptor_set_data_impl(RBDescriptorSetLayout layout, void* buffer, size_t buffer_size) override;
    
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
    void create_command_pool();
    
    void create_render_finished_semaphores();

private: // Special section
    void cleanup_swapchain();
    void recreate_swapchain();
    void create_images_context();

    void create_depth_resources();
    
private: // Camera section
    // void create_camera_ubo();
    void update_camera_ubo(vk::FrameContext& FrameContext, const Camera& camera);
    RBDescriptorSetLayout allocate_descriptor_layout_handle();
    virtual RBDescriptorSetLayout create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout) override;
    void create_descriptor_pool();
    virtual void allocate_descriptor_sets_for_layout(
        RBDescriptorSetLayout layout_handle,
        DescriptorPoolType pool_type) override;

public:
    void bind_descriptor_set(RBCommandList cmd, int i, RBDescriptorSet rb_descriptors, RBPipelineHandle pipeline_handle) override;
    RBFrameHandle get_current_frame() const override;
    
    virtual void wait_for_frame(RBFrameHandle frame_handle) override;
    void advance_frame() override;
    void bind_mesh(const RBCommandList& cmd, MeshHandle mesh) override;
    void push_constants(const RBCommandList& cmd, glm::mat4 matrix) override;
    void draw_indexed(const RBCommandList& cmd, uint32_t index_count) override;

private:
    vk::Context context = {};
    vk::InstanceContext instance_context = {};
    vk::SwapchainContext swapchain_context = {};
    vk::CommandContext command_context = {};
    vk::FrameScheduleContext frame_schedule_context = {};
    vk::PipelineContext pipeline_context = {};
    vk::DescriptorContext descriptor_context = {};
    std::vector<vk::ImageContext> images;
    
    
    std::map<RBPipelineHandle, std::unique_ptr<VkPipelineObject>> pipelines;
    
    uint32_t current_image_index = 0;
    
    bool framebuffer_resized = false;
    
    GLFWwindow* window = nullptr;
    
    uint64_t descriptor_set_counter = 0;
    std::map<RBDescriptorSetLayout, DescriptorSetLayoutData> descriptor_set_layouts;
    std::map<RBDescriptorSetLayout, RBDescriptorSet> persistent_descriptors;
};
