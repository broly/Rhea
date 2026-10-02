module;

#include <json/value.h>

export module game:screen_jpeg;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import cvar;
import :jpeg;
#include "object/object_reflection_macro.h"

/************************************************************************
 * SETTINGS (render.jpeg.screen.*)
 ***********************************************************************/

export cvar::Var<bool> cv_screen_jpeg_enabled(
    "render.jpeg.screen.enabled", true, "Full screen JPEG damage: the frame is compressed harder the lower the health and on hits");
export cvar::Var<float> cv_screen_jpeg_threshold(
    "render.jpeg.screen.threshold", 0.6f, "Health (0..1) below which the frame starts to compress",
    { .has_range = true, .min = 0.05f, .max = 1.0f });
export cvar::Var<float> cv_screen_jpeg_curve(
    "render.jpeg.screen.curve", 1.5f, "Power of the damage curve below the threshold: higher keeps the frame clean longer",
    { .has_range = true, .min = 0.25f, .max = 4.0f });
export cvar::Var<int> cv_screen_jpeg_quality_start(
    "render.jpeg.screen.quality_start", 35, "JPEG quality at the start of the damage (just below the threshold)",
    { .has_range = true, .min = 1.0f, .max = 100.0f });
export cvar::Var<int> cv_screen_jpeg_quality_min(
    "render.jpeg.screen.quality_min", 4, "JPEG quality at zero health",
    { .has_range = true, .min = 1.0f, .max = 100.0f });
export cvar::Var<int> cv_screen_jpeg_downscale_max(
    "render.jpeg.screen.downscale_max", 3, "Downscale factor at zero health (1: full resolution)",
    { .has_range = true, .min = 1.0f, .max = 16.0f });
export cvar::Var<int> cv_screen_jpeg_generations_max(
    "render.jpeg.screen.generations_max", 3, "JPEG generations (recompressions with a shifted grid) at zero health",
    { .has_range = true, .min = 1.0f, .max = 16.0f });
export cvar::Var<JpegChroma> cv_screen_jpeg_chroma(
    "render.jpeg.screen.chroma", JpegChroma::Yuv420, "Chroma subsampling of the damage JPEG");
export cvar::Var<JpegFilter> cv_screen_jpeg_filter(
    "render.jpeg.screen.filter", JpegFilter::Box, "Downscale of the damage JPEG: Nearest pixels or Box soap");
export cvar::Var<float> cv_screen_jpeg_sharpen(
    "render.jpeg.screen.sharpen", 0.25f, "Sharpening after the damage JPEG (block edges and ringing stand out)",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_screen_jpeg_hit_strength(
    "render.jpeg.screen.hit_strength", 0.6f, "Damage a hit of strength 1 adds on top of the health (0..1 of the curve)",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_screen_jpeg_hit_decay(
    "render.jpeg.screen.hit_decay", 0.35f, "How fast the hit burst fades, s (time constant)",
    { .has_range = true, .min = 0.02f, .max = 3.0f });
export cvar::Var<float> cv_screen_jpeg_debug_health(
    "render.jpeg.screen.debug_health", -1.0f, "Overrides the player's health for the effect (negative: off)",
    { .has_range = true, .min = -1.0f, .max = 1.0f }, cvar::none);

/************************************************************************
 * STATE
 ***********************************************************************/

// Full screen JPEG damage (J3): the tone mapped frame goes through the shakalizer chain "ScreenJpeg" of
// GameRenderGraph before the UI. Gameplay reports the health and the hits; the render graph ticks it once a
// frame and reads the settings.
//   severity = ((threshold - health) / threshold) ^ curve, 0 above the threshold
//   a hit adds a burst that decays exponentially: total = 1 - (1 - severity)(1 - burst)
//   quality quality_start -> quality_min, downscale 1 -> downscale_max, generations 1 -> generations_max
export namespace screen_jpeg
{
    // health of the player, 0..1
    void set_health(float health);
    // a hit: a burst of damage, strength 0..1 (a new grid seed, so the blocks jump)
    void hit(float strength = 1.0f);

    struct State
    {
        bool active = false;
        // share of the shakalized frame in the output, 0..1
        float amount = 0.0f;
        // 0..1 of the curve, health and burst together
        float severity = 0.0f;
        JpegSettings settings;
    };

    // once a frame, before evaluate
    void tick(float delta_seconds);
    State evaluate();
}
