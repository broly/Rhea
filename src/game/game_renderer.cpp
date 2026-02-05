module game:renderer;

import render;
import vk;
import glm;
import rhmath;
import <vector>;
import profile;
import name;
import <unordered_map>;
import rhcomponents;
import :render_graph;
import :constants;
import texture_format;

#include "render_layout.h"
#include "common/assertion_macros.h"
#include "profiling/profile.h"

void GameRenderer::set_engine(const std::shared_ptr<Engine>& in_engine)
{
    engine = in_engine;
}


constexpr bool debug_shadow_pass = false;

void GameRenderer::init(RBWindowHandle in_window)
{
    
    render_backend = RenderBackend::create<VkRenderBackend>(in_window);
    
    
    samplers["default"] = render_backend->create_sampler(samplers::default_surface());
    samplers["surface"] = render_backend->create_sampler(samplers::default_surface());
    samplers["shadow"] = render_backend->create_sampler(samplers::default_shadow());
    
    
    Renderer::init(in_window);
    
    create_render_graph(main_render_graph_name, {{"capture_ibl", true}}, "ibl");
    // trigger_aux_rg_once("ibl");
}

void GameRenderer::capture_ibl(glm::vec3 pos, Name actor_name)
{
    current_cubemaps.insert({actor_name, Cubemap{}});
    
    for (int index = 0; index < 6; index++)
    {
        RenderGraphParameters params;
        params.bool_params["capture_ibl"] = true;
        params.int_params["face_index"] = index;
        params.vec3_params["capture_pos"] = pos;
        trigger_aux_rg_once("ibl", params, [=] (RenderGraphContext& ctx)
        {
            std::vector<float> buffer;
            TextureFormat fmt;
            auto image_to_read = ctx.render_graph.get_image_by_name("hdr_color");
            checkf(image_to_read.has_value(), "Unable to get image");
            ctx.backend.copy_image_to_buffer(
                    *image_to_read,
                    buffer,
                    fmt,
                    Constants::ibl_extent
                );
            current_cubemaps[actor_name].faces[index] = buffer;
            current_cubemaps[actor_name].format = fmt;
            current_cubemaps[actor_name].face_size = 512;
            
            if (index == 5)
            {
                finish_capturing_ibl(actor_name);
            }
        });
    }
    
}

void GameRenderer::finish_capturing_ibl(Name actor_name)
{
    const uint32_t face_size = 512;
    const uint32_t channels  = 4;

    const uint32_t width  = face_size;
    const uint32_t height = face_size * 6;

    std::vector<float> exr_pixels(
        width * height * channels
    );

    for (int face = 0; face < 6; ++face)
    {
        const std::vector<float>& face_buf = current_cubemaps[actor_name].faces[face];
        checkf(face_buf.size() == face_size * face_size * channels,
               "Invalid face buffer size");

        for (uint32_t y = 0; y < face_size; ++y)
        {
            const size_t src_offset =
                (y * face_size) * channels;

            const size_t dst_offset =
                ((face * face_size + y) * width) * channels;

            memcpy(
                &exr_pixels[dst_offset],
                &face_buf[src_offset],
                face_size * channels * sizeof(float)
            );
        }
    }

    auto name = std::string("hdr/ibl_atlas_") + actor_name.to_string() + ".exr";
    current_cubemaps[actor_name].save(name);

    current_cubemaps.erase(actor_name);
}
