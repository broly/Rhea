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
import <filesystem>;
import paths;
import :cubemap_capture_render_graph;

#include "render_layout.h"
#include "common/assertion_macros.h"
#include "profiling/profile.h"

import log;
#include "logging/log_macro.h"

DEFINE_LOGGER(LogGameRenderer, DisplayFn);

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
    
    auto aux_graph = reflect::get_object_type_name<CubemapCaptureRenderGraph>();
    create_render_graph(aux_graph, {{"capture_ibl", true}}, "ibl");
}

void GameRenderer::capture_ibl(glm::vec3 pos, Name actor_name)
{
    LogGameRenderer.Log("Starting IBL capture for %s", actor_name.to_string().c_str());
    current_cubemaps.insert({ actor_name, Cubemap{} });

    RenderGraphParameters params;
    params.bool_params["capture_ibl"] = true;
    params.vec3_params["capture_pos"] = pos;

    trigger_aux_rg_once("ibl", params,
        [=](RenderGraphContext& ctx)
        {
            auto image_opt =
                ctx.render_graph.get_image_by_name("prefiltered_env");

            checkf(image_opt.has_value(),
                   "Unable to get IBL cubemap image");

            // ---- READBACK ----
            ImageReadback readback =
                ctx.backend.readback_image(*image_opt);

            checkf(readback.layers == 6,
                   "IBL cubemap must have 6 layers");

            Cubemap& cubemap = current_cubemaps[actor_name];
            cubemap.format    = readback.format;
            cubemap.face_size = readback.extent.width;

            for (uint32_t face = 0; face < 6; ++face)
            {
                cubemap.faces[face] =
                    std::move(readback.layers_data[face]);
            }

            finish_capturing_ibl(actor_name);
        });
}

void GameRenderer::finish_capturing_ibl(Name actor_name)
{
    Cubemap& cubemap = current_cubemaps[actor_name];

    checkf(cubemap.face_size > 0,
           "Cubemap not initialized");

    auto name =
        std::string("hdr/ibl_") +
        actor_name.to_string() +
        ".exr";

    std::filesystem::path path = paths::get_cache_path() / name;
    cubemap.save(path);

    current_cubemaps.erase(actor_name);
}

