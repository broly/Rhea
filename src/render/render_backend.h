#pragma once
#include <memory>
#include <span>

#include "graphics_pipeline.h"
#include "framework/camera.h"
#include "handle_types.h"
#include "pipeline_object.h"
#include "rg_types.h"
#include "scene_extractor.h"


struct RenderGraphPass;
class RenderBackend;

template<typename T>
concept RenderBackendType = 
    std::is_base_of_v<RenderBackend, T> && 
        requires(T t, RBWindowHandle window_handle) { t.init(window_handle); };

class RenderBackend 
{
public:
    virtual ~RenderBackend() = default;
    virtual RBFrameHandle get_current_frame() const = 0;
    virtual void wait_for_frame(RBFrameHandle frame) = 0;
    virtual void reset_frame_fence(RBFrameHandle frame) = 0;
    virtual void advance_frame() = 0;
    virtual RBDescriptorSet get_descriptor_set(RBDescriptorSetLayout rb_descriptor_set_layout, ResourceUsageType pool_type) = 0;

    virtual RBBufferHandle create_uniform_buffer(size_t buffer_size, ResourceUsageType usage_type) = 0;
    virtual void update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data) = 0;
    virtual void bind_buffer_to_descriptor(RBDescriptorSetLayout layout, uint32_t binding, RBBufferHandle buffer) = 0;
    
    template<typename T>
    void update_uniform_buffer(RBBufferHandle buffer_handle, T& data)
    {
        update_uniform_buffer_impl(buffer_handle, sizeof(T), (void*)&data);
    }

    virtual RBSwapchainExtent get_swapchain_extent() const = 0;
    virtual void update_depth_descriptor(const RBDescriptorSet& rb_handle, RBImageHandle value, RGTextureFormat format) = 0;


    template<RenderBackendType T>
    static std::unique_ptr<RenderBackend> create(RBWindowHandle window_handle)
    {
        auto result = std::make_unique<T>();
        result->init(window_handle);
        return result;
    }
    
    // ---- commands section ----
    virtual RBCommandList begin_commands(RBFrameHandle frame_handle) = 0;
    virtual void end_commands(RBCommandList cmd_list) = 0;
    
    // ---- pass section ----
    virtual void begin_render_pass(RBCommandList cmd_list, RBFramebufferId framebuffer_index) = 0;
    virtual void end_render_pass(RBCommandList cmd_list) = 0;
    
    virtual void bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object) = 0;
    
    virtual void draw(RBCommandList cmd_list, uint32_t vertex_count) = 0;
    
    virtual RBDescriptorSetLayout create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout) = 0;
    
    virtual void allocate_descriptor_sets_for_layout(
        RBDescriptorSetLayout layout_handle,
        ResourceUsageType pool_type) = 0;

    virtual bool acquire_next_image(RBFrameHandle frame_handle) = 0;
    virtual void submit_frame(RBFrameHandle frame_handle, RBCommandList cmd_list) = 0;
    
    virtual PipelineObject* create_pipeline(GraphicsPipelineDesc desc) = 0;


    virtual void bind_descriptor_set(RBCommandList cmd, int i, RBDescriptorSet rb_descriptors, RBPipelineHandle pipeline_handle) = 0;
    
    virtual void bind_mesh(const RBCommandList& cmd, MeshHandle mesh) = 0;
    virtual void push_constants(const RBCommandList& cmd, glm::mat4 matrix, RBPipelineHandle pipeline_handle) = 0;
    virtual void draw_indexed(const RBCommandList& cmd, uint32_t index_count) = 0;
    virtual void get_or_create_mesh_buffers(MeshHandle handle) = 0;
    virtual RGTextureFormat get_swapchain_format() const = 0;
    virtual RBImageHandle create_image(const RBImageDesc& desc) = 0;
    virtual RBImageView get_image_view(RBImageHandle handle) = 0;
    virtual RBFramebufferId get_or_create_framebuffer(const FramebufferDesc& desc) = 0;
    virtual RBImageView resolve_image_view(const RGTexture& tex, RBFrameHandle frame) = 0;
    virtual RBImageView get_swapchain_image_view(RBFrameHandle frame) = 0;
    virtual RBImageHandle get_swapchain_image() const = 0;
    virtual RBRenderPass get_or_create_render_pass(const FramebufferDesc& fb) = 0;
    virtual RBSampler create_sampler(const RBSamplerDesc& desc) = 0;
    virtual void bind_image_to_descriptor(
        RBDescriptorSetLayout layout,
        uint32_t binding,
        RBImageHandle image,
        RBSampler sampler) = 0;
    
    virtual void CRUTCH_transition_image(
        RBCommandList cmd,
        RBImageHandle image,
        RGTextureFormat format,
        VkImageLayout old_layout,
        VkImageLayout new_layout) = 0;
    
    virtual void draw_fullscreen(RBCommandList cmd) = 0;
    
    virtual void update_sampled_image(
        RBDescriptorSetLayout layout,
        uint32_t binding,
        RBImageHandle image,
        ResourceUsageType usage) = 0;
};
