#pragma once
#include <memory>
#include <span>

#include "graphics_pipeline.h"
#include "framework/camera.h"
#include "handle_types.h"
#include "scene_extractor.h"
#include "backends/vk/vk_camera_ubo.h"


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
    virtual void advance_frame() = 0;
    virtual RBDescriptorSet get_descriptor_set(RBDescriptorSetLayout rb_descriptor_set_layout, DescriptorPoolType pool_type) = 0;

    template<typename T>
    void update_descriptor_set_data(RBDescriptorSetLayout rb_handle, const T& buffer)
    {
        update_descriptor_set_data_impl(rb_handle, (void*)&buffer, sizeof(T));
    }
    
    virtual void update_descriptor_set_data_impl(RBDescriptorSetLayout rb_handle, void* buffer, size_t buffer_size) = 0;
    
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
    
    virtual void bind_pipeline(RBCommandList cmd_list, RBPipelineHandle pipeline_handle) = 0;
    
    virtual void draw(RBCommandList cmd_list, uint32_t vertex_count) = 0;
    
    virtual RBDescriptorSetLayout create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout) = 0;
    
    virtual void allocate_descriptor_sets_for_layout(
        RBDescriptorSetLayout layout_handle,
        DescriptorPoolType pool_type) = 0;

    virtual RBFramebufferId acquire_next_image(RBFrameHandle frame_handle) = 0;
    virtual void submit_frame(RBFrameHandle frame_handle, RBCommandList cmd_list, RBFramebufferId framebuffer_id) = 0;
    
    virtual RBPipelineHandle create_pipeline(GraphicsPipelineDesc desc) = 0;


    virtual void bind_descriptor_set(RBCommandList cmd, int i, RBDescriptorSet rb_descriptors, RBPipelineHandle pipeline_handle) = 0;
    
    virtual void bind_mesh(const RBCommandList& cmd, MeshHandle mesh) = 0;
    virtual void push_constants(const RBCommandList& cmd, glm::mat4 matrix, RBPipelineHandle pipeline_handle) = 0;
    virtual void draw_indexed(const RBCommandList& cmd, uint32_t index_count) = 0;
    virtual void create_mesh_buffers(MeshHandle handle) = 0;
};
