#pragma once
#include <memory>

#include "handle_types.h"
#include "framework/camera.h"



class RenderBackend;

template<typename T>
concept RenderBackendType = 
    std::is_base_of_v<RenderBackend, T> && 
        requires(T t, RBWindowHandle window_handle) { t.init(window_handle); };

class RenderBackend 
{
public:
    virtual ~RenderBackend() = default;

    template<RenderBackendType T>
    static std::unique_ptr<RenderBackend> create(RBWindowHandle window_handle)
    {
        auto result = std::make_unique<T>();
        result->init(window_handle);
        return result;
    }
    
    // ---- commands section ----
    virtual RBCommandList begin_commands() = 0;
    virtual void end_commands(RBCommandList cmd_list) = 0;
    
    // ---- pass section ----
    virtual void begin_render_pass(RBCommandList cmd_list, RBFramebufferId framebuffer_index) = 0;
    virtual void end_render_pass(RBCommandList cmd_list) = 0;
    
    virtual void bind_pipeline(RBCommandList cmd_list, RBPipelineHandle pipeline_handle) = 0;
    
    virtual void draw(RBCommandList cmd_list, uint32_t vertex_count) = 0;
    
    virtual void update_camera_ubo(const Camera& camera) = 0;

    virtual RBFramebufferId acquire_next_image() = 0;
    virtual void submit_frame(RBCommandList cmd_list) = 0;

    
    virtual RBPipelineHandle get_pipeline_handle() const = 0;
    virtual RBDescriptorSet get_camera_descriptor_set() const = 0;
    virtual void bind_descriptor_set(RBCommandList cmd, int i, RBDescriptorSet rb_descriptors) = 0;
    
    
    

    // virtual void init(void* window) = 0;
    // virtual void draw_frame(const Camera& camera) = 0;
};
