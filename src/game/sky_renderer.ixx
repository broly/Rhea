module;

#include <json/value.h>

export module game:sky_renderer;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render;
import render_scene;
import glm;
import name;
import cvar;
import rhmath;
#include "object/object_reflection_macro.h"

// sky_march.comp
struct SkyMarchPushConstants
{
    uint32_t steps;
    uint32_t light_steps;
    float history_weight;
    uint32_t frame;
    uint32_t downscale;
};
RH_REGISTER_TYPE(SkyMarchPushConstants)

// Cost of the sky: texels of its buffers (downscale), and for the clouds the long steps per ray (steps) and
// the samples towards the light per sample inside a cloud (light_steps). The history hides the noise of the
// dithered cloud samples.
export cvar::Var<int> cv_sky_downscale(
    "render.sky.downscale", 2, "Screen pixels per texel of the sky buffers (atmosphere, clouds), per axis (1-8)");
export cvar::Var<bool> cv_clouds_enabled(
    "render.clouds.enabled", true, "Volumetric clouds (off: the sky without them)");
export cvar::Var<int> cv_clouds_steps(
    "render.clouds.steps", 64, "Long steps of the march per cloud ray (8-256); inside a cloud it takes 3 short ones for each");
export cvar::Var<int> cv_clouds_light_steps(
    "render.clouds.light_steps", 6, "Samples towards the light per cloud sample (1-16)");
export cvar::Var<float> cv_clouds_history(
    "render.clouds.history", 0.9f, "Share of the reprojected previous frames in the cloud buffer (0: none, noisy; 0.95: smooth, lags)");

// Sky of the main view: the SkyAtmosphere / VolumetricClouds of the level (SceneViewProcessor_Sky) and its
// directional light as a frame of rendering.
//
//   prepare         the SkyUBO (resource "sky": atmosphere, celestial bodies, cloud layer, noise volumes),
//                   and the CPU estimates the rest of the frame takes from the sky (ambient)
//   dispatch_march  pass "SkyMarch" (compute): the atmosphere and the clouds marched into buffers of
//                   1 / downscale of the screen, where sky is visible; the clouds are blended with their
//                   reprojected history
//   draw_sky        pass "Sky": the buffers upsampled behind the scene, with sun, moon and stars at the
//                   full resolution between them
//
// The reflection probes capture the same sky through the "sky" resource (probe_sky.frag, coarser clouds).
// The marches themselves are shaders/sky/atmosphere.glsl and shaders/sky/clouds.glsl, shared by every pass
// that shows the sky.
//
// Without a SkyAtmosphere in the level the sky is a default atmosphere without clouds.
export class SkyRenderer
{
public:
    struct FrameInputs
    {
        glm::vec3 camera_position;
        double time = 0.0;
        // the buffers of this frame, and the clouds of the previous one (a layer of the history texture)
        RBImageHandle atmosphere;
        RBImageHandle clouds;
        RBImageHandle clouds_history;
        uint32_t history_layer = 0;
        // screen pixels per texel of the buffers, per axis
        uint32_t downscale = 1;
    };

    void init(Renderer& renderer, RenderBackend& backend);

    void prepare(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs);

    // extent: of the sky buffers
    void dispatch_march(RenderGraphContext& ctx, RenderResource* camera, RenderResource* gbuffer, Extent extent);
    void draw_sky(RenderGraphContext& ctx, RenderResource* camera, RenderResource* gbuffer);

    // Average radiance of the clear sky over the upper hemisphere, cosine weighted: irradiance / pi of a
    // surface facing up. The ambient of what no reflection probe covers.
    glm::vec3 get_sky_ambient() const { return sky_ambient; }
    // Coverage of the cloud layer drawn this frame, 0 without clouds
    float get_cloud_coverage() const { return cloud_coverage; }

private:
    Renderer* renderer = nullptr;
    RenderBackend* backend = nullptr;

    RenderResource* sky_resource = nullptr;
    RenderResource* buffers_resource = nullptr;
    RenderResource* targets_resource = nullptr;

    std::shared_ptr<PipelineFamily> march_family;
    std::shared_ptr<PipelineFamily> sky_family;
    PipelineObject* march_pipeline = nullptr;
    PipelineObject* sky_pipeline = nullptr;

    // tiling noise volumes of the clouds (tools/sky/generate_cloud_noise.py)
    RBImageHandle cloud_shape;
    RBImageHandle cloud_detail;

    uint32_t downscale = 1;
    uint32_t frame_counter = 0;
    uint32_t history_frames = 0;    // frames marched into the current buffers
    RBImageHandle history_image;    // recreated (resize, downscale): it holds no history
    float cloud_coverage = 0.0f;
    glm::vec3 sky_ambient = glm::vec3(0.0f);
};
