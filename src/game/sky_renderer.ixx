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

// cloud_shadow.frag
struct CloudShadowPushConstants
{
    uint32_t steps;
};
RH_REGISTER_TYPE(CloudShadowPushConstants)

// fog_march.frag
struct FogMarchPushConstants
{
    uint32_t steps;
    float history_weight;
    uint32_t frame;
    uint32_t downscale;
};
RH_REGISTER_TYPE(FogMarchPushConstants)

// fog.frag
struct FogPushConstants
{
    uint32_t downscale;
};
RH_REGISTER_TYPE(FogPushConstants)

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
export cvar::Var<bool> cv_clouds_shadows(
    "render.clouds.shadows", true, "Shadows of the clouds on the scene (their strength: VolumetricClouds::shadow)");
export cvar::Var<int> cv_clouds_shadow_steps(
    "render.clouds.shadow_steps", 32, "Samples through the cloud layer per texel of the cloud shadow map (4-128)");

// Cost of the fog: texels of its buffer (downscale) x samples per ray (steps), each with a tap of the shadow
// cascades, of the cloud shadow and two of the noise (and the reflection probes once in 8 samples). The
// history hides the noise of the dithered samples.
export cvar::Var<bool> cv_fog_enabled(
    "render.fog.enabled", true, "Volumetric fog and light shafts (the VolumetricFog of the level)");
export cvar::Var<int> cv_fog_downscale(
    "render.fog.downscale", 2, "Screen pixels per texel of the fog buffer, per axis (1-8)");
export cvar::Var<int> cv_fog_steps(
    "render.fog.steps", 32, "Samples of the fog per ray (8-128)");
export cvar::Var<float> cv_fog_history(
    "render.fog.history", 0.85f, "Share of the reprojected previous frames in the fog buffer (0: none, noisy; 0.95: smooth, lags)");

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
//   draw_cloud_shadow  pass "CloudShadow": what the clouds let through of the directional light, as a map
//                   across that light around the camera (shaders/resources/shadow.glsl reads it with the
//                   shadow cascades: lighting, reflection probe captures)
//   draw_fog_march  pass "FogMarch": the VolumetricFog of the level marched from the eye into a buffer of
//                   1 / fog downscale of the screen, lit through the shadow cascades and the cloud shadow
//                   (the light shafts) and by the reflection probes, blended with its reprojected history
//   draw_fog        pass "Fog": that buffer upsampled by depth and blended over the lit frame, sky included
//
// The reflection probes capture the same sky through the "sky" resource (probe_sky.frag, coarser clouds).
// The marches themselves are shaders/sky/atmosphere.glsl and shaders/sky/clouds.glsl, shared by every pass
// that shows the sky; the fog is shaders/sky/fog.glsl (the probes capture none).
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
        // the fog buffer of this frame and of the previous one (a layer of its history texture), and its downscale
        RBImageHandle fog;
        RBImageHandle fog_history;
        uint32_t fog_downscale = 1;
    };

    void init(Renderer& renderer, RenderBackend& backend);

    void prepare(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs);

    // extent: of the sky buffers
    void dispatch_march(RenderGraphContext& ctx, RenderResource* camera, RenderResource* gbuffer, Extent extent);
    void draw_sky(RenderGraphContext& ctx, RenderResource* camera, RenderResource* gbuffer);

    // Any fog to draw this frame (set by prepare): the level has a VolumetricFog with some density
    bool has_fog() const { return fog_active; }
    // Fills the bound target, the fog buffer of `extent` (leaves its viewport set). light, shadow: with the
    // shadow cascades and the cloud shadow map of the frame; reflection: the probes (the light from around)
    void draw_fog_march(RenderGraphContext& ctx, RenderResource* camera, RenderResource* gbuffer, RenderResource* light,
        RenderResource* shadow, RenderResource* reflection, Extent extent);
    // Blends the fog over the bound target (the lit frame)
    void draw_fog(RenderGraphContext& ctx, RenderResource* gbuffer);

    // Cloud shadow map: cloud_shadow_texels^2 texels over cloud_shadow_size^2 meters across the directional
    // light, centered on the camera. 8 m per texel: the shapes of the clouds are tens of meters
    static constexpr uint32_t cloud_shadow_texels = 512;
    static constexpr float cloud_shadow_size = 4096.0f;
    // Projection of this frame's map, as the light UBO carries it (DirectionalLight::cloud_shadow_*).
    // strength 0: no cloud shadows (no clouds, no light above the horizon, switched off).
    struct CloudShadow
    {
        glm::vec4 u{ 0.0f };
        glm::vec4 v{ 0.0f };
        glm::vec4 params{ 0.0f };
    };
    const CloudShadow& get_cloud_shadow() const { return cloud_shadow; }
    // Fills the bound target of cloud_shadow_texels^2 (leaves its viewport set). light: the light UBO with
    // get_cloud_shadow() in it.
    void draw_cloud_shadow(RenderGraphContext& ctx, RenderResource* light);

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
    RenderResource* fog_buffers_resource = nullptr;

    std::shared_ptr<PipelineFamily> march_family;
    std::shared_ptr<PipelineFamily> sky_family;
    std::shared_ptr<PipelineFamily> cloud_shadow_family;
    std::shared_ptr<PipelineFamily> fog_march_family;
    std::shared_ptr<PipelineFamily> fog_family;
    PipelineObject* march_pipeline = nullptr;
    PipelineObject* sky_pipeline = nullptr;
    PipelineObject* cloud_shadow_pipeline = nullptr;
    PipelineObject* fog_march_pipeline = nullptr;
    PipelineObject* fog_pipeline = nullptr;

    // tiling noise volumes of the clouds (tools/sky/generate_cloud_noise.py)
    RBImageHandle cloud_shape;
    RBImageHandle cloud_detail;

    uint32_t downscale = 1;
    uint32_t frame_counter = 0;
    uint32_t history_frames = 0;    // frames marched into the current buffers
    RBImageHandle history_image;    // recreated (resize, downscale): it holds no history
    float cloud_coverage = 0.0f;
    glm::vec3 sky_ambient = glm::vec3(0.0f);
    CloudShadow cloud_shadow;

    bool fog_active = false;
    uint32_t fog_downscale = 1;
    uint32_t fog_history_frames = 0;    // frames marched into the current fog buffer
    RBImageHandle fog_history_image;    // recreated (resize, downscale): it holds no history
};
