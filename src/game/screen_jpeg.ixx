module;

#include <json/value.h>

export module game:screen_jpeg;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;
import glm;

import cvar;
import :jpeg;
#include "object/object_reflection_macro.h"

/************************************************************************
 * SETTINGS (render.jpeg.screen.*)
 ***********************************************************************/

export cvar::Var<bool> cv_screen_jpeg_enabled(
    "render.jpeg.screen.enabled", true, "Full screen JPEG damage: a vignette of compression at low health, a pulse on hits");
export cvar::Var<JpegChroma> cv_screen_jpeg_chroma(
    "render.jpeg.screen.chroma", JpegChroma::Yuv420, "Chroma subsampling of the damage JPEG");
export cvar::Var<JpegFilter> cv_screen_jpeg_filter(
    "render.jpeg.screen.filter", JpegFilter::Box, "Downscale of the damage JPEG: Nearest pixels or Box soap");
export cvar::Var<float> cv_screen_jpeg_sharpen(
    "render.jpeg.screen.sharpen", 0.25f, "Sharpening after the damage JPEG (block edges and ringing stand out)",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<int> cv_screen_jpeg_block(
    "render.jpeg.screen.block", 16, "The compressed frame shows in whole blocks of this many px, dithered at the edge of the "
    "vignette and in the fade of a hit (0: a smooth mix)", { .has_range = true, .min = 0.0f, .max = 64.0f });
export cvar::Var<float> cv_screen_jpeg_debug_health(
    "render.jpeg.screen.debug_health", -1.0f, "Overrides the player's health for the effect (negative: off)",
    { .has_range = true, .min = -1.0f, .max = 1.0f }, cvar::none);

// low health: the vignette
export cvar::Var<float> cv_screen_jpeg_threshold(
    "render.jpeg.screen.health.threshold", 0.75f, "Health (0..1) below which the vignette of compression appears",
    { .has_range = true, .min = 0.05f, .max = 1.0f });
export cvar::Var<float> cv_screen_jpeg_curve(
    "render.jpeg.screen.health.curve", 1.3f, "Power of the health curve below the threshold: higher keeps the vignette "
    "small longer", { .has_range = true, .min = 0.25f, .max = 4.0f });
export cvar::Var<float> cv_screen_jpeg_radius_start(
    "render.jpeg.screen.health.radius_start", 1.25f, "Clean radius of the vignette at the threshold (1: the top and bottom "
    "edges, 1.67: the corners of 16:9)", { .has_range = true, .min = 0.0f, .max = 2.0f });
export cvar::Var<float> cv_screen_jpeg_radius_min(
    "render.jpeg.screen.health.radius_min", 0.22f, "Clean radius of the vignette at zero health (tunnel vision)",
    { .has_range = true, .min = 0.0f, .max = 2.0f });
export cvar::Var<float> cv_screen_jpeg_feather(
    "render.jpeg.screen.health.feather", 0.35f, "Width of the vignette's edge (same units as the radius)",
    { .has_range = true, .min = 0.01f, .max = 2.0f });
export cvar::Var<int> cv_screen_jpeg_quality_start(
    "render.jpeg.screen.health.quality_start", 20, "JPEG quality of the vignette just below the threshold",
    { .has_range = true, .min = 1.0f, .max = 100.0f });
export cvar::Var<int> cv_screen_jpeg_quality_min(
    "render.jpeg.screen.health.quality_min", 4, "JPEG quality of the vignette at zero health",
    { .has_range = true, .min = 1.0f, .max = 100.0f });
export cvar::Var<int> cv_screen_jpeg_downscale_max(
    "render.jpeg.screen.health.downscale_max", 3, "Downscale of the vignette at zero health (1: full resolution)",
    { .has_range = true, .min = 1.0f, .max = 16.0f });
export cvar::Var<int> cv_screen_jpeg_generations_max(
    "render.jpeg.screen.health.generations_max", 3, "JPEG generations (recompressions with a shifted grid) of the vignette at "
    "zero health", { .has_range = true, .min = 1.0f, .max = 16.0f });
export cvar::Var<float> cv_screen_jpeg_health_noise(
    "render.jpeg.screen.health.noise", 0.0f, "Noise before the codec at zero health (the vignette's blocks shimmer)",
    { .has_range = true, .min = 0.0f, .max = 1.0f });

// hits: the pulse over the whole screen
export cvar::Var<float> cv_screen_jpeg_hit_strength(
    "render.jpeg.screen.hit.strength", 1.0f, "Pulse a hit of strength 1 starts (0..1)",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_screen_jpeg_hit_decay(
    "render.jpeg.screen.hit.decay", 0.3f, "How fast the JPEG pulse of a hit fades, s (time constant)",
    { .has_range = true, .min = 0.02f, .max = 3.0f });
export cvar::Var<float> cv_screen_jpeg_hit_fade(
    "render.jpeg.screen.hit.fade", 0.35f, "Pulse below which the full screen JPEG falls apart into blocks (0..1); above "
    "it covers the whole screen", { .has_range = true, .min = 0.01f, .max = 1.0f });
export cvar::Var<int> cv_screen_jpeg_hit_quality(
    "render.jpeg.screen.hit.quality", 5, "JPEG quality at the peak of the pulse",
    { .has_range = true, .min = 1.0f, .max = 100.0f });
export cvar::Var<int> cv_screen_jpeg_hit_downscale_x(
    "render.jpeg.screen.hit.downscale_x", 8, "Horizontal downscale at the peak of the pulse (rows smear)",
    { .has_range = true, .min = 1.0f, .max = 16.0f });
export cvar::Var<int> cv_screen_jpeg_hit_downscale_y(
    "render.jpeg.screen.hit.downscale_y", 2, "Vertical downscale at the peak of the pulse",
    { .has_range = true, .min = 1.0f, .max = 16.0f });
export cvar::Var<int> cv_screen_jpeg_hit_generations(
    "render.jpeg.screen.hit.generations", 3, "JPEG generations at the peak of the pulse",
    { .has_range = true, .min = 1.0f, .max = 16.0f });
export cvar::Var<float> cv_screen_jpeg_hit_noise(
    "render.jpeg.screen.hit.noise", 0.3f, "Noise before the codec at the peak of the pulse: new every frame, the blocks "
    "flicker", { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<bool> cv_screen_jpeg_hit_flicker(
    "render.jpeg.screen.hit.flicker", true, "While the pulse is strong the grid of the generations and the block dither "
    "jump every frame");
export cvar::Var<float> cv_screen_jpeg_red(
    "render.jpeg.screen.hit.red", 0.85f, "Red edges at the peak of the pulse (0..1)",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_screen_jpeg_red_decay(
    "render.jpeg.screen.hit.red_decay", 0.45f, "How fast the red edges fade, s (time constant)",
    { .has_range = true, .min = 0.02f, .max = 3.0f });
export cvar::Var<float> cv_screen_jpeg_red_radius(
    "render.jpeg.screen.hit.red_radius", 0.5f, "Where the red edges start (1: the top and bottom edges)",
    { .has_range = true, .min = 0.0f, .max = 2.0f });
export cvar::Var<glm::vec3> cv_screen_jpeg_red_color(
    "render.jpeg.screen.hit.red_color", glm::vec3(1.0f, 0.06f, 0.04f), "Color of the red edges", { .color = true });

/************************************************************************
 * STATE
 ***********************************************************************/

// Full screen JPEG damage (J3): the tone mapped frame goes through the shakalizer chain "ScreenJpeg" of
// GameRenderGraph before the UI, and the pass Present shows the result where the damage is. One JPEG, two
// contributions with their own parameters and their own share of the screen:
//   health  severity = ((threshold - health) / threshold) ^ curve, 0 above the threshold
//           -> a vignette: clean radius radius_start -> radius_min (tunnel vision), quality quality_start ->
//              quality_min, downscale 1 -> downscale_max, generations 1 -> generations_max
//   hit     a pulse that decays exponentially, the whole screen while it is above hit.fade
//           -> quality hit.quality, downscale hit.downscale_x x hit.downscale_y (rows smear), noise before the
//              codec (the blocks flicker), red edges (their own decay)
// The chain takes the harsher value of each parameter (lower quality, larger downscale, more generations).
export namespace screen_jpeg
{
    // health of the player, 0..1
    void set_health(float health);
    // a hit: a pulse, strength 0..1 (a new grid seed, so the blocks jump)
    void hit(float strength = 1.0f);

    struct State
    {
        bool active = false;
        // share of the shakalized frame over the whole screen (the hit pulse), 0..1
        float amount = 0.0f;
        // the health vignette (JpegPresentParams); radius above 2: off
        float vignette_radius = 10.0f;
        float vignette_feather = 0.3f;
        float block = 0.0f;
        uint32_t seed = 0;
        // red edges: color x strength, from radius
        glm::vec3 tint{ 0.0f };
        float tint_radius = 0.5f;
        // 0..1: the health curve and the pulse
        float health_severity = 0.0f;
        float pulse = 0.0f;
        JpegSettings settings;
    };

    // once a frame, before evaluate
    void tick(float delta_seconds);
    State evaluate();
}
