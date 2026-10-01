module;

#include <json/value.h>

export module game:gtao_renderer;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render;
import glm;
import name;
import cvar;
import rhmath;
#include "object/object_reflection_macro.h"

// gtao.comp
struct GTAOPushConstants
{
    glm::vec4 params;           // radius (m), falloff share, thin occluder compensation, power
    uint32_t slices;
    uint32_t steps;
    uint32_t downscale;
    uint32_t pad = 0;
};
RH_REGISTER_TYPE(GTAOPushConstants)

// gtao_denoise.comp
struct GTAODenoisePushConstants
{
    float blur_beta;
    uint32_t final_pass;
    uint32_t enabled;
    uint32_t pad = 0;
};
RH_REGISTER_TYPE(GTAODenoisePushConstants)

// Cost of GTAO: texels (downscale) x slices x 2 sides x steps depth samples, plus two 3 x 3 denoise passes
export cvar::Var<bool> cv_gtao_enabled(
    "render.gtao.enabled", true, "Screen space ambient occlusion (GTAO) of the indirect light");
export cvar::Var<int> cv_gtao_downscale(
    "render.gtao.downscale", 2, "Screen pixels per GTAO texel, per axis (1: full resolution)",
    { .has_range = true, .min = 1.0f, .max = 2.0f });
export cvar::Var<float> cv_gtao_radius(
    "render.gtao.radius", 0.75f, "World space radius of the occluder search, m",
    { .has_range = true, .min = 0.05f, .max = 5.0f });
export cvar::Var<int> cv_gtao_slices(
    "render.gtao.slices", 2, "Planes around the view ray searched for horizons per pixel",
    { .has_range = true, .min = 1.0f, .max = 8.0f });
export cvar::Var<int> cv_gtao_steps(
    "render.gtao.steps", 3, "Depth samples per side of a slice",
    { .has_range = true, .min = 1.0f, .max = 8.0f });
export cvar::Var<float> cv_gtao_falloff(
    "render.gtao.falloff", 0.6f, "Share of the radius the occluders fade out over",
    { .has_range = true, .min = 0.05f, .max = 1.0f });
export cvar::Var<float> cv_gtao_thin_compensation(
    "render.gtao.thin_compensation", 0.0f, "Occluders in front are taken as thin: less occlusion from them (0: off)",
    { .has_range = true, .min = 0.0f, .max = 0.7f });
export cvar::Var<float> cv_gtao_power(
    "render.gtao.power", 2.2f, "Power of the visibility: higher darkens the occlusion",
    { .has_range = true, .min = 0.5f, .max = 5.0f });
export cvar::Var<bool> cv_gtao_denoise(
    "render.gtao.denoise", true, "Edge aware blur of the GTAO noise (off: the raw samples)");
export cvar::Var<float> cv_gtao_blur_beta(
    "render.gtao.blur_beta", 1.2f, "Center weight of the GTAO denoiser: lower blurs more",
    { .has_range = true, .min = 0.1f, .max = 10.0f });
export cvar::Var<float> cv_gtao_foliage(
    "render.gtao.foliage", 0.5f, "Strength of GTAO on leaves (alpha tested crowns occlude themselves a lot)",
    { .has_range = true, .min = 0.0f, .max = 1.0f });

// Screen space ambient occlusion (GenericRenderGraph, main graph only), XeGTAO without the depth mip chain:
//   pass GTAO           gtao.comp: visibility, depth and edges per texel of 1 / downscale of the screen -> raw
//   pass GTAODenoise1   raw -> mid, edge aware 3 x 3 blur
//   pass GTAODenoise2   mid -> result, the same with a stronger center
// The lighting pass reads the result (resource "gtao", upsampled by depth when downscaled) and multiplies
// the indirect light (diffuse and specular occlusion) with it.
export class GtaoRenderer
{
public:
    void init(Renderer& renderer, RenderBackend& backend);

    struct FrameInputs
    {
        uint32_t downscale = 1;
        RBImageHandle raw;
        RBImageHandle mid;
        RBImageHandle result;
    };
    void prepare(RenderGraphContext& ctx, const FrameInputs& inputs);

    // extent: of the GTAO targets
    void dispatch_gtao(RenderGraphContext& ctx, RenderResource* camera, RenderResource* gbuffer, Extent extent);
    // pass 0: raw -> mid, 1: mid -> result
    void dispatch_denoise(RenderGraphContext& ctx, uint32_t pass, Extent extent);

private:
    Renderer* renderer = nullptr;
    RenderBackend* backend = nullptr;

    RenderResource* main_resource = nullptr;
    std::array<RenderResource*, 2> denoise_resources{};

    std::shared_ptr<PipelineFamily> gtao_family;
    std::array<std::shared_ptr<PipelineFamily>, 2> denoise_families;
    PipelineObject* gtao_pipeline = nullptr;
    std::array<PipelineObject*, 2> denoise_pipelines{};

    uint32_t downscale = 1;
};
