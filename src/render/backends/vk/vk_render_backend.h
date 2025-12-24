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

struct MeshGPUData {
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory index_memory = VK_NULL_HANDLE;
    uint32_t index_count = 0;
    RBDescriptorSet descriptor_set;
};

struct FramebufferResource
{
    VkFramebuffer framebuffer;
    VkRenderPass  render_pass;
    uint32_t      width;
    uint32_t      height;
};



class VkRenderBackend final : public RenderBackend
{
    friend class VkPipelineObject;
public:
    void init(RBWindowHandle window);
    // virtual void draw_frame(const Camera& camera) override;
    
    RBCommandList begin_commands(RBFrameHandle frame_handle) override;
    void end_commands(RBCommandList cmd_list) override;
    void begin_render_pass(RBCommandList cmd_list, RBFramebufferId framebuffer_index) override;
    void end_render_pass(RBCommandList cmd_list) override;
    void bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object) override;
    void draw(RBCommandList cmd_list, uint32_t vertex_count) override;
    void acquire_next_image(RBFrameHandle frame_handle) override;
    void submit_frame(RBFrameHandle frame_handle, RBCommandList cmd_list) override;
    PipelineObject* create_pipeline(GraphicsPipelineDesc desc) override;
    DescriptorSetLayoutData get_vk_descriptor_set_layout(const RBDescriptorSetLayout& rb_handle);
    virtual RBDescriptorSet get_descriptor_set(RBDescriptorSetLayout rb_descriptor_set_layout, ResourceUsageType pool_type) override;
    RBBufferHandle create_uniform_buffer(size_t buffer_size, ResourceUsageType usage_type) override;
    
    
    virtual void update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data) override;
    void bind_buffer_to_descriptor(RBDescriptorSetLayout layout, uint32_t binding, RBBufferHandle buffer) override;
    RBSwapchainExtent get_swapchain_extent() const override;

    vk::BufferInfo& get_buffer(RBBufferHandle buffer_handle, size_t frame_index = 0);
    
    RenderPassDesc make_render_pass_desc(const FramebufferDesc& fb) const;

    
private: // Initialization section
    void create_instance();
    void match_queue_families();
    void create_device();
    void create_frame_sync_objects();
    
private: // Re-/Initialization section

    void create_swapchain();
    void create_command_pool();

private: // Special section
    void cleanup_swapchain();
    void recreate_swapchain();

    void create_depth_resources();
    
private: // Camera section
    RBDescriptorSetLayout allocate_descriptor_layout_handle();
    virtual RBDescriptorSetLayout create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout) override;
    void create_descriptor_pool();
    virtual void allocate_descriptor_sets_for_layout(
        RBDescriptorSetLayout layout_handle,
        ResourceUsageType usage_type) override;

public:
    void bind_descriptor_set(RBCommandList cmd, int i, RBDescriptorSet rb_descriptors, RBPipelineHandle pipeline_handle) override;
    RBFrameHandle get_current_frame() const override;
    
    virtual void wait_for_frame(RBFrameHandle frame_handle) override;
    void advance_frame() override;
    void bind_mesh(const RBCommandList& cmd, MeshHandle mesh) override;
    void push_constants(const RBCommandList& cmd, glm::mat4 matrix, RBPipelineHandle pipeline_handle) override;
    void draw_indexed(const RBCommandList& cmd, uint32_t index_count) override;
    void get_or_create_mesh_buffers(MeshHandle handle) override;
    RGTextureFormat get_swapchain_format() const override;
    RBImageHandle create_image(const RBImageDesc& desc) override;
    RBImageView get_image_view(RBImageHandle handle, RBFrameHandle frame_handle) override;
    RBFramebufferId get_or_create_framebuffer(const FramebufferDesc& desc) override;
    RBImageView resolve_image_view(const RGTexture& tex, RBFrameHandle frame) override;
    RBImageView get_swapchain_image_view(RBFrameHandle frame) override;
    RBImageHandle get_swapchain_image(RBFrameHandle frame) const override;
    
    
    virtual RBRenderPass get_or_create_render_pass(const FramebufferDesc& fb) override;
    
    VkFormat get_image_format(RBImageHandle handle) const;
    
    RBPipelineHandle get_or_create_pipeline(
        RBPipelineHandle handle,
        VkRenderPass render_pass);

private:
    vk::InstanceContext instance_context = {};
    vk::SwapchainContext swapchain_context = {};
    vk::CommandContext command_context = {};
    vk::FrameScheduleContext frame_schedule_context = {};
    vk::PipelineContext pipeline_context = {};
    vk::DescriptorContext descriptor_context = {};
    std::vector<vk::ImageResource> image_resources;
    
    // currently working pipelines (moving from pending_pipelines)
    std::map<RBPipelineHandle, std::unique_ptr<VkPipelineObject>> pipelines;
    
    // pipelines objects that only have layouts, but not real pipelines yet
    std::vector<std::unique_ptr<VkPipelineObject>> pending_pipelines;
    
    uint32_t current_image_index = 0;
    
    bool framebuffer_resized = false;
    
    GLFWwindow* window = nullptr;
    
    uint64_t descriptor_set_counter = 0;
    std::map<RBDescriptorSetLayout, DescriptorSetLayoutData> descriptor_set_layouts;
    std::map<RBDescriptorSetLayout, RBDescriptorSet> persistent_descriptors;
    std::map<MeshHandle, MeshGPUData> mesh_map;
    std::unordered_map<size_t, VkFramebuffer> framebuffer_cache;
    std::vector<FramebufferResource> framebuffer_resources;
    
    std::unordered_map<RenderPassDesc, VkRenderPass, RenderPassDescHash> render_pass_cache;
    std::vector<RBImageHandle> swapchain_image_handles;
    VkRenderPass current_render_pass = VK_NULL_HANDLE;
};
