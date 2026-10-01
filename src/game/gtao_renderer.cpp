module game;

import :gtao_renderer;

import std.compat;
import assertions;

import render;
import glm;
import rhmath;
import name;
import profile;

#include "common/assertion_macros.h"
#include "profiling/profile.h"

namespace
{
    // local size of gtao.comp and gtao_denoise.comp
    constexpr uint32_t group = 8;

    ComputeWorkgroups groups_for(Extent extent)
    {
        return { (extent.width + group - 1) / group, (extent.height + group - 1) / group, 1 };
    }
}

void GtaoRenderer::init(Renderer& in_renderer, RenderBackend& in_backend)
{
    renderer = &in_renderer;
    backend = &in_backend;

    main_resource = renderer->find_resource("gtao_main");
    denoise_resources[0] = renderer->find_resource("gtao_denoise_1");
    denoise_resources[1] = renderer->find_resource("gtao_denoise_2");
    checkf(main_resource && denoise_resources[0] && denoise_resources[1],
        "GTAO resources are missing (assets/render/resources/gtao_*.json)");

    auto model = renderer->find_model("gtao");
    checkf(model, "material model 'gtao' is missing (assets/render/schemas/gtao.json)");
    gtao_family = renderer->query_pipeline_family("GTAO", model);
    denoise_families[0] = renderer->query_pipeline_family("GTAODenoise1", model);
    denoise_families[1] = renderer->query_pipeline_family("GTAODenoise2", model);
}

void GtaoRenderer::prepare(RenderGraphContext& ctx, const FrameInputs& inputs)
{
    PROFILE("GtaoRenderer::prepare");

    // pipelines are requested every frame: hot reload drops the PSO cache
    gtao_pipeline = gtao_family->request_pipeline({});
    for (size_t pass = 0; pass < denoise_pipelines.size(); ++pass)
        denoise_pipelines[pass] = denoise_families[pass]->request_pipeline({});

    main_resource->update_image("u_gtao_raw_target", inputs.raw, { .frame = ctx.frame });
    denoise_resources[0]->update_image("u_gtao_denoise_source", inputs.raw, { .frame = ctx.frame });
    denoise_resources[0]->update_image("u_gtao_denoise_target", inputs.mid, { .frame = ctx.frame });
    denoise_resources[1]->update_image("u_gtao_denoise_source", inputs.mid, { .frame = ctx.frame });
    denoise_resources[1]->update_image("u_gtao_denoise_target", inputs.result, { .frame = ctx.frame });

    downscale = std::max(inputs.downscale, 1u);
}

void GtaoRenderer::dispatch_gtao(RenderGraphContext& ctx, RenderResource* camera, RenderResource* gbuffer, Extent extent)
{
    PROFILE("GtaoRenderer::dispatch_gtao");

    ctx.bind_pipeline(gtao_pipeline);
    ctx.bind(camera, gbuffer, main_resource);
    ctx.push_constants(GTAOPushConstants{
        .params = glm::vec4(
            std::clamp(cv_gtao_radius.get(), 0.05f, 5.0f),
            std::clamp(cv_gtao_falloff.get(), 0.05f, 1.0f),
            std::clamp(cv_gtao_thin_compensation.get(), 0.0f, 0.7f),
            std::clamp(cv_gtao_power.get(), 0.5f, 5.0f)),
        .slices = uint32_t(std::clamp(cv_gtao_slices.get(), 1, 8)),
        .steps = uint32_t(std::clamp(cv_gtao_steps.get(), 1, 8)),
        .downscale = downscale,
    });
    ctx.compute(groups_for(extent));
}

void GtaoRenderer::dispatch_denoise(RenderGraphContext& ctx, uint32_t pass, Extent extent)
{
    PROFILE("GtaoRenderer::dispatch_denoise");
    check(pass < denoise_pipelines.size());

    ctx.bind_pipeline(denoise_pipelines[pass]);
    ctx.bind(denoise_resources[pass]);
    ctx.push_constants(GTAODenoisePushConstants{
        .blur_beta = std::clamp(cv_gtao_blur_beta.get(), 0.1f, 10.0f),
        .final_pass = pass == denoise_pipelines.size() - 1 ? 1u : 0u,
        .enabled = cv_gtao_denoise.get() ? 1u : 0u,
    });
    ctx.compute(groups_for(extent));
}
