#include "render_graph.h"

#include "backends/vk/vk_render_backend.h"


void RenderGraph::initialize(RBWindowHandle window_handle)
{
    backend = RenderBackend::create<VkRenderBackend>(window_handle);
    
}

void RenderGraph::draw(const Camera& camera)
{
    RBFramebufferId framebuffer_index = backend->acquire_next_image(); // image_available semaphore
    auto pipeline_handle = backend->get_pipeline_handle();
    auto cmd = backend->begin_commands();                                // reset cmd buffer
    backend->update_camera_ubo(camera);
    backend->begin_render_pass(cmd, framebuffer_index);
    auto camera_set = backend->get_camera_descriptor_set();
    backend->bind_pipeline(cmd, pipeline_handle);
    backend->bind_descriptor_set(cmd, 0, camera_set);
    backend->draw(cmd, 36);
    backend->end_render_pass(cmd);
    backend->end_commands(cmd);
    backend->submit_frame(cmd);

}

