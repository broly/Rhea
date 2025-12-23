#include "render_graph.h"

#include "backends/vk/vk_render_backend.h"


RGResourceHandle RenderGraph::create_texture(const RGTextureDesc& desc)
{
    RGResourceHandle handle{
        static_cast<uint32_t>(resources.size())
    };

    RGResource res{};
    res.kind = RGResourceKind::Texture;
    res.texture_desc = desc;

    resources.push_back(std::move(res));

    return handle;
}

RGResourceHandle RenderGraph::create_buffer(const RGBufferDesc&)
{
    RGResourceHandle handle;
    handle.id = static_cast<uint32_t>(resources.size());

    resources.push_back({ RGResourceKind::Buffer });
    return handle;
}

RGPassId RenderGraph::add_pass(RenderGraphPass&& pass)
{
    RGPassId id{ static_cast<uint32_t>(passes.size()) };
    passes.push_back(std::move(pass));
    return id;
}

void RenderGraph::compile()
{
    execution_order.clear();
    execution_order.reserve(passes.size());

    for (uint32_t i = 0; i < passes.size(); ++i)
    {
        execution_order.push_back(i);
    }
}

void RenderGraph::execute(RenderBackend& backend)
{
    RBFrameHandle frame = backend.get_current_frame();
    backend.wait_for_frame(frame);

    RBFramebufferId fb = backend.acquire_next_image(frame);
    RBCommandList cmd = backend.begin_commands(frame);

    RenderGraphContext ctx(backend, cmd, fb);

    for (uint32_t pass_index : execution_order)
    {
        RenderGraphPass& pass = passes[pass_index];

        if (!pass.writes.empty())
        {
            backend.begin_render_pass(cmd, fb);
        }

        pass.execute(ctx);

        if (!pass.writes.empty())
        {
            backend.end_render_pass(cmd);
        }
    }

    backend.end_commands(cmd);
    backend.submit_frame(frame, cmd, fb);
    backend.advance_frame();
}


// void RenderGraph::initialize(RBWindowHandle window_handle)
// {
//     backend = RenderBackend::create<VkRenderBackend>(window_handle);
//     
// }
//
// void RenderGraph::draw(const Camera& camera)
// {
//     auto frame_id = backend->get_current_frame();
//
//     backend->wait_for_frame(frame_id);
//
//     RBFramebufferId framebuffer_index = backend->acquire_next_image(frame_id);
//
//     auto cmd = backend->begin_commands(frame_id);
//
//     backend->update_camera_ubo(frame_id, camera);
//
//     backend->begin_render_pass(cmd, framebuffer_index);
//
//     auto pipeline_handle = backend->get_pipeline_handle();
//     backend->bind_pipeline(cmd, pipeline_handle);
//
//     auto camera_set = backend->get_camera_descriptor_set();
//     backend->bind_descriptor_set(cmd, 0, camera_set);
//
//     backend->draw(cmd, 36);
//
//     backend->end_render_pass(cmd);
//     backend->end_commands(cmd);
//
//     backend->submit_frame(frame_id, cmd, framebuffer_index);
//
//     backend->advance_frame();
// }
//

