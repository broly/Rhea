module;

#include <json/value.h>

export module game:post_process;

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

// bloom_downsample.comp, bloom_upsample.comp
struct BloomPushConstants
{
    glm::vec4 params;       // x: 1 for the first down level (Karis average), y: upsample scatter
};
RH_REGISTER_TYPE(BloomPushConstants)

// auto_exposure.comp
struct AutoExposurePushConstants
{
    glm::vec4 range;        // x: min EV, y: max EV, z: log2 of the key, w: frame time (s)
    glm::vec4 speed;        // x: speed up, y: speed down, z: low percentile, w: high percentile
    glm::vec4 histogram;    // x: log2 luminance of the first bin, y: log2 luminance range, z: center weight, w: 1 resets
};
RH_REGISTER_TYPE(AutoExposurePushConstants)

// Tone curve of the post processing. Keep in sync with TONEMAPPER_* in shaders/utils/tonemapping.slang
export enum class Tonemapper : uint8_t
{
    None,           // clamp
    Reinhard,       // x / (1 + x): soft, flat
    ACESFast,       // Narkowicz fit of ACES: contrasty, saturated (the look Rhea had before the post processing)
    ACES,           // Hill fit of the ACES RRT + ODT: film like, darker midtones
    Filmic,         // Hable (Uncharted 2)
    AgX,            // Sobotka: bright colors go to white without hue skews
    AgXPunchy,      // AgX with more contrast and saturation
    Neutral,        // Khronos PBR Neutral: base colors kept, only highlights compressed
};

export enum class ExposureMode : uint8_t
{
    Manual,         // render.post.exposure.ev
    Auto,           // eye adaptation: the metered average luminance is brought to the key
};

/************************************************************************
 * SETTINGS (render.post.*): presets set every one of them except the switches below
 ***********************************************************************/

export cvar::Var<bool> cv_post_enabled(
    "render.post.enabled", true, "Post processing. Off: exposure, tone curve and display gamma only (no bloom, grading, lens effects)");
export cvar::Var<std::string> cv_post_preset(
    "render.post.preset", "Default", "Name of the last applied post process preset (assets/render/post_process_presets.json)");
export cvar::Var<bool> cv_post_show_histogram(
    "render.post.exposure.show_histogram", false, "Draws the luminance histogram of the auto exposure (bottom left): yellow the metered average, white the exposed middle gray, red the EV limits",
    {}, cvar::none);

// exposure
export cvar::Var<ExposureMode> cv_post_exposure_mode(
    "render.post.exposure.mode", ExposureMode::Auto, "Manual: a fixed EV. Auto: eye adaptation to the brightness of the frame");
export cvar::Var<float> cv_post_exposure_ev(
    "render.post.exposure.ev", 0.0f, "Manual exposure: the frame is multiplied by 2^EV",
    { .has_range = true, .min = -10.0f, .max = 10.0f });
export cvar::Var<float> cv_post_exposure_compensation(
    "render.post.exposure.compensation", 0.0f, "Added to the exposure (manual and auto), EV",
    { .has_range = true, .min = -5.0f, .max = 5.0f });
export cvar::Var<float> cv_post_exposure_min_ev(
    "render.post.exposure.min_ev", -4.0f, "Auto exposure: the lowest exposure, EV (limits how dark a bright scene gets)",
    { .has_range = true, .min = -10.0f, .max = 10.0f });
export cvar::Var<float> cv_post_exposure_max_ev(
    "render.post.exposure.max_ev", 4.0f, "Auto exposure: the highest exposure, EV (limits how much a dark interior or the night is brightened)",
    { .has_range = true, .min = -10.0f, .max = 10.0f });
export cvar::Var<float> cv_post_exposure_key(
    "render.post.exposure.key", 0.18f, "Auto exposure: the value the metered average luminance is exposed to (middle gray)",
    { .has_range = true, .min = 0.03f, .max = 0.6f });
export cvar::Var<float> cv_post_exposure_speed_up(
    "render.post.exposure.speed_up", 3.0f, "Auto exposure: adaptation speed towards a brighter scene (1 / s)",
    { .has_range = true, .min = 0.05f, .max = 20.0f });
export cvar::Var<float> cv_post_exposure_speed_down(
    "render.post.exposure.speed_down", 1.0f, "Auto exposure: adaptation speed towards a darker scene (1 / s)",
    { .has_range = true, .min = 0.05f, .max = 20.0f });
export cvar::Var<float> cv_post_exposure_low_percent(
    "render.post.exposure.low_percent", 0.1f, "Auto exposure: share of the darkest pixels left out of the metering",
    { .has_range = true, .min = 0.0f, .max = 0.98f });
export cvar::Var<float> cv_post_exposure_high_percent(
    "render.post.exposure.high_percent", 0.9f, "Auto exposure: the metering stops at this share (the brightest pixels, sky and lamps, are left out)",
    { .has_range = true, .min = 0.02f, .max = 1.0f });
export cvar::Var<float> cv_post_exposure_center_weight(
    "render.post.exposure.center_weight", 0.5f, "Auto exposure: how much more the middle of the screen counts than the border",
    { .has_range = true, .min = 0.0f, .max = 1.0f });

// bloom
export cvar::Var<float> cv_post_bloom_intensity(
    "render.post.bloom.intensity", 0.04f, "Bloom: share of the frame replaced by its blurred copy (no threshold: every light glows by its brightness)",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_post_bloom_scatter(
    "render.post.bloom.scatter", 0.7f, "Bloom: weight of the wider levels (higher: a wider, softer glow)",
    { .has_range = true, .min = 0.0f, .max = 0.95f });
export cvar::Var<glm::vec3> cv_post_bloom_tint(
    "render.post.bloom.tint", glm::vec3(1.0f), "Bloom: color of the glow", { .color = true });

// tone curve and display
export cvar::Var<Tonemapper> cv_post_tonemapper(
    "render.post.tonemapper", Tonemapper::ACESFast, "Tone curve: how the HDR frame is compressed to the display");
export cvar::Var<float> cv_post_gamma(
    "render.post.gamma", 2.2f, "Display gamma (the brightness slider of games: lower is brighter). Rhea used 3.7 before the post processing",
    { .has_range = true, .min = 1.0f, .max = 4.0f });

// color grading
export cvar::Var<float> cv_post_temperature(
    "render.post.color.temperature", 0.0f, "White balance: negative cooler (blue), positive warmer (orange)",
    { .has_range = true, .min = -100.0f, .max = 100.0f });
export cvar::Var<float> cv_post_tint(
    "render.post.color.tint", 0.0f, "White balance: negative green, positive magenta",
    { .has_range = true, .min = -100.0f, .max = 100.0f });
export cvar::Var<glm::vec3> cv_post_color_filter(
    "render.post.color.filter", glm::vec3(1.0f), "Multiplies the HDR color (after the white balance)", { .color = true });
export cvar::Var<float> cv_post_contrast(
    "render.post.color.contrast", 1.0f, "Contrast around middle gray (log space, before the tone curve)",
    { .has_range = true, .min = 0.5f, .max = 2.0f });
export cvar::Var<float> cv_post_saturation(
    "render.post.color.saturation", 1.0f, "Saturation (0: black and white)",
    { .has_range = true, .min = 0.0f, .max = 2.0f });
export cvar::Var<float> cv_post_hue_shift(
    "render.post.color.hue_shift", 0.0f, "Rotates every hue, degrees",
    { .has_range = true, .min = -180.0f, .max = 180.0f });
export cvar::Var<glm::vec3> cv_post_lift(
    "render.post.color.lift", glm::vec3(0.0f), "Lift: added to the shadows (display space, -0.2..0.2 per channel)");
export cvar::Var<glm::vec3> cv_post_midtones(
    "render.post.color.gamma", glm::vec3(1.0f), "Gamma of the midtones per channel (higher: brighter midtones)");
export cvar::Var<glm::vec3> cv_post_gain(
    "render.post.color.gain", glm::vec3(1.0f), "Gain: multiplies the highlights per channel");
export cvar::Var<glm::vec3> cv_post_split_shadows(
    "render.post.color.split_shadows", glm::vec3(0.2f, 0.45f, 0.65f), "Split toning: color laid over the shadows (soft light)", { .color = true });
export cvar::Var<float> cv_post_split_shadows_strength(
    "render.post.color.split_shadows_strength", 0.0f, "Split toning: strength of the shadows color",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<glm::vec3> cv_post_split_highlights(
    "render.post.color.split_highlights", glm::vec3(1.0f, 0.7f, 0.4f), "Split toning: color laid over the highlights (soft light)", { .color = true });
export cvar::Var<float> cv_post_split_highlights_strength(
    "render.post.color.split_highlights_strength", 0.0f, "Split toning: strength of the highlights color",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_post_split_balance(
    "render.post.color.split_balance", 0.0f, "Split toning: positive moves the border towards the shadows (more highlights color)",
    { .has_range = true, .min = -1.0f, .max = 1.0f });
export cvar::Var<int> cv_post_posterize(
    "render.post.color.posterize", 0, "Levels per color channel (0: off; 4-8 for a retro look)",
    { .has_range = true, .min = 0.0f, .max = 32.0f });

// lens
export cvar::Var<float> cv_post_vignette_intensity(
    "render.post.vignette.intensity", 0.25f, "Vignette: darkening of the corners",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_post_vignette_smoothness(
    "render.post.vignette.smoothness", 0.4f, "Vignette: softness of its border",
    { .has_range = true, .min = 0.01f, .max = 1.0f });
export cvar::Var<float> cv_post_vignette_roundness(
    "render.post.vignette.roundness", 1.0f, "Vignette: 1 a circle, 0 follows the screen aspect",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<glm::vec3> cv_post_vignette_color(
    "render.post.vignette.color", glm::vec3(0.0f), "Vignette: color of the corners", { .color = true });
export cvar::Var<float> cv_post_chromatic_aberration(
    "render.post.chromatic_aberration", 0.0f, "Chromatic aberration: red / blue offset at the screen edges, share of the screen",
    { .has_range = true, .min = 0.0f, .max = 0.02f });
export cvar::Var<float> cv_post_sharpen(
    "render.post.sharpen", 0.2f, "Sharpening of the luminance (brings back the detail TAA blurs)",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_post_grain_intensity(
    "render.post.grain.intensity", 0.0f, "Film grain: strength",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_post_grain_size(
    "render.post.grain.size", 1.5f, "Film grain: size of the grains, px",
    { .has_range = true, .min = 1.0f, .max = 4.0f });
export cvar::Var<float> cv_post_grain_response(
    "render.post.grain.response", 0.8f, "Film grain: how much it fades in the highlights",
    { .has_range = true, .min = 0.0f, .max = 1.0f });

/************************************************************************
 * PRESETS: assets/render/post_process_presets.json, a list of
 * { "name", "description", "values": { "<render.post.* cvar>": value } }.
 * Applying a preset resets every setting above to its default and sets the listed ones.
 ***********************************************************************/

export namespace post_process
{
    struct Preset
    {
        std::string name;
        std::string description;
        std::vector<std::pair<std::string, std::string>> values;   // cvar name, text value
    };

    // the settings presets change: render.post.* without the switches (enabled, preset, show_histogram)
    std::vector<cvar::Entry*> get_settings();

    // loaded on first use
    const std::vector<Preset>& get_presets();
    void reload_presets();
    const Preset* find_preset(std::string_view name);
    bool apply_preset(std::string_view name);
    // the settings that differ from their defaults, as preset `name` (replaced if it exists); writes the file
    bool save_preset(std::string_view name, std::string_view description);
    std::filesystem::path get_presets_path();
}

/************************************************************************
 * RENDERER
 ***********************************************************************/

// Post processing of the main graph (GameRenderGraph), after TAA, before the tone mapping pass which
// composes it (shaders/tonemap.slang):
//   passes BloomDown0..5   bloom_downsample.comp: the HDR frame into 1/2 .. 1/64 of the screen
//   pass AutoExposure      auto_exposure.comp: histogram of the 1/16 level -> adapted exposure (64 x 2 RGBA32F)
//   passes BloomUp4..0     bloom_upsample.comp: back up to 1/2 of the screen, blended with the down levels
// Every pass binds its own instance (descriptor set) of the resource "post_process": the descriptor set
// indices are global and the device allows 32, so the post processing has one resource for everything.
export class PostProcessRenderer
{
public:
    static constexpr uint32_t bloom_levels = 6;
    // the bloom level the auto exposure meters (1/16 of the screen)
    static constexpr uint32_t exposure_level = 3;
    static constexpr Extent exposure_extent = { 64, 2 };
    // the luminance range of the histogram: 2^-10 .. 2^10
    static constexpr float histogram_log_min = -10.0f;
    static constexpr float histogram_log_range = 20.0f;

    void init(Renderer& renderer, RenderBackend& backend);

    struct FrameInputs
    {
        RBImageHandle scene;    // the lit HDR frame
        std::array<RBImageHandle, bloom_levels> down;
        std::array<RBImageHandle, bloom_levels - 1> up;
        RBImageHandle exposure;
        Extent screen;
        float delta_seconds = 0.0f;
    };
    // descriptors and the PostProcessUBO of the frame
    void prepare(RenderGraphContext& ctx, const FrameInputs& inputs);

    bool bloom_enabled() const;
    bool auto_exposure_enabled() const;

    // level: of the down chain (target)
    void dispatch_downsample(RenderGraphContext& ctx, uint32_t level, Extent extent);
    // level: of the up chain (target), from bloom_levels - 2 down to 0
    void dispatch_upsample(RenderGraphContext& ctx, uint32_t level, Extent extent);
    void dispatch_auto_exposure(RenderGraphContext& ctx);
    // the instance the tone mapping pass reads (after its pipeline is bound)
    void bind_tonemap(RenderGraphContext& ctx);

private:
    PostProcessUBO build_ubo(Extent screen) const;

    static constexpr uint32_t up_instance(uint32_t level) { return bloom_levels + level; }
    static constexpr uint32_t exposure_instance = 2 * bloom_levels;
    static constexpr uint32_t tonemap_instance = 2 * bloom_levels + 1;

    Renderer* renderer = nullptr;
    RenderBackend* backend = nullptr;
    RenderResource* resource = nullptr;

    std::shared_ptr<PipelineFamily> downsample_family;
    std::shared_ptr<PipelineFamily> upsample_family;
    std::shared_ptr<PipelineFamily> exposure_family;
    PipelineObject* downsample_pipeline = nullptr;
    PipelineObject* upsample_pipeline = nullptr;
    PipelineObject* exposure_pipeline = nullptr;

    float delta_seconds = 0.0f;
    uint32_t frame = 0;
    // the adapted exposure starts at the target: first frame, auto exposure switched on
    bool exposure_reset = true;
    bool was_auto = false;
};
