#include "render_graph.h"

#include "backends/vk/vk_render_backend.h"


void RenderGraph::initialize(RBWindowHandle window_handle)
{
    backend = RenderBackend::create<VkRenderBackend>(window_handle);
    
}

void RenderGraph::draw(const Camera& camera)
{
    auto frame_id = backend->get_current_frame();

    backend->wait_for_frame(frame_id);

    RBFramebufferId framebuffer_index = backend->acquire_next_image(frame_id);

    auto cmd = backend->begin_commands(frame_id);

    backend->update_camera_ubo(frame_id, camera);

    backend->begin_render_pass(cmd, framebuffer_index);

    auto pipeline_handle = backend->get_pipeline_handle();
    backend->bind_pipeline(cmd, pipeline_handle);

    auto camera_set = backend->get_camera_descriptor_set();
    backend->bind_descriptor_set(cmd, 0, camera_set);

    backend->draw(cmd, 36);

    backend->end_render_pass(cmd);
    backend->end_commands(cmd);

    backend->submit_frame(frame_id, cmd, framebuffer_index);

    backend->advance_frame();
}


