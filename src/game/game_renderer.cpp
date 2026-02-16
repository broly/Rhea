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
import :brdf_lut_capture_render_graph;

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
    samplers["depth"] = render_backend->create_sampler(samplers::default_depth());
    
    
    Renderer::init(in_window);
    
    // auto aux_graph1 = reflect::get_object_type_name<CubemapCaptureRenderGraph>();
    // create_render_graph(aux_graph1, {{"capture_ibl", true}}, "ibl");
}
void GameRenderer::capture_ibl(glm::vec3 pos, Name actor_name)
{
    LogGameRenderer.Log("Starting IBL capture for %s",
        actor_name.to_string().c_str());
    
    for (Name image_type : {"irradiance", "prefiltered_env"})
        current_cubemaps.insert({ actor_name.to_string() + "_" + image_type.to_string(), Cubemap{} });

    RenderGraphParameters params;
    params.bool_params["capture_ibl"] = true;
    params.vec3_params["capture_pos"] = pos;

    for (Name image_type : {"irradiance", "prefiltered_env"})
    {
        trigger_aux_rg_once("ibl", params,
            [=](RenderGraphContext& ctx)
            {
                auto image_opt =
                    ctx.render_graph.get_image_by_name(image_type);

                checkf(image_opt.has_value(),
                       "Unable to get IBL cubemap image");

                // --------------------------------------------------
                // Readback
                // --------------------------------------------------

                ImageReadback readback =
                    ctx.backend.readback_image(*image_opt);
                
                const std::string image_name = actor_name.to_string() + "_" + image_type.to_string();

                checkf(readback.layers == 6, "should be 6 layers (cubemap)");
                Cubemap& cubemap = current_cubemaps[image_name];

                cubemap.format    = readback.format;
                cubemap.face_size = readback.extent.width;

                // --------------------------------------------------
                // Copy faces + mips
                // --------------------------------------------------

                for (uint32_t face = 0; face < 6; ++face)
                {
                    auto& dst_face = cubemap.faces[face];
                    dst_face.clear();
                    dst_face.resize(readback.mips);

                    for (uint32_t mip = 0; mip < readback.mips; ++mip)
                    {
                        // move is fine: ImageReadback is temporary
                        dst_face[mip] =
                            std::move(readback.data[face][mip]);
                    }
                }

                finish_capturing_ibl(image_name);
            });
    }
}

void GameRenderer::finish_capturing_ibl(const std::string& filename)
{
    Cubemap& cubemap = current_cubemaps[filename];

    checkf(cubemap.face_size > 0,
           "Cubemap not initialized");

    auto name =
        std::string("hdr/ibl_") +
        filename +
        ".exr";

    std::filesystem::path path = paths::get_cache_path() / name;
    cubemap.save(path);

    current_cubemaps.erase(filename);
}

