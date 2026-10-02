module;

#include <json/value.h>

export module game:jpeg_hits;

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
 * SETTINGS (render.jpeg.hits.*)
 ***********************************************************************/

export cvar::Var<bool> cv_jpeg_hits_enabled(
    "render.jpeg.hits.enabled", true, "JPEG spots where hits land: the 8 x 8 blocks of the screen over the hit surfaces are compressed");
export cvar::Var<int> cv_jpeg_hits_quality(
    "render.jpeg.hits.quality", 3, "JPEG quality of a fresh hit (strength 1)",
    { .has_range = true, .min = 1.0f, .max = 100.0f });
export cvar::Var<int> cv_jpeg_hits_quality_weak(
    "render.jpeg.hits.quality_weak", 30, "JPEG quality of a faded hit (strength near 0)",
    { .has_range = true, .min = 1.0f, .max = 100.0f });
export cvar::Var<int> cv_jpeg_hits_generations(
    "render.jpeg.hits.generations", 2, "JPEG generations of the hit spots",
    { .has_range = true, .min = 1.0f, .max = 16.0f });
export cvar::Var<JpegChroma> cv_jpeg_hits_chroma(
    "render.jpeg.hits.chroma", JpegChroma::Yuv420, "Chroma subsampling of the hit spots");
export cvar::Var<float> cv_jpeg_hits_sharpen(
    "render.jpeg.hits.sharpen", 0.3f, "Sharpening of the hit spots",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_jpeg_hits_edge(
    "render.jpeg.hits.edge", 0.5f, "Ragged border of a spot: share of the radius where blocks are dropped at random",
    { .has_range = true, .min = 0.0f, .max = 1.0f });
export cvar::Var<float> cv_jpeg_hits_radius(
    "render.jpeg.hits.radius", 0.6f, "Radius of a hit when the caller gives none (render.jpeg.hits.shoot), m",
    { .has_range = true, .min = 0.05f, .max = 10.0f });
export cvar::Var<float> cv_jpeg_hits_lifetime(
    "render.jpeg.hits.lifetime", 2.5f, "Lifetime of a hit when the caller gives none, s",
    { .has_range = true, .min = 0.1f, .max = 30.0f });

/************************************************************************
 * HITS
 ***********************************************************************/

// JPEG spots of the hits (J4): spheres in the world with a lifetime. Every frame the pass JpegHitMask marks the
// 8 x 8 blocks of the screen whose surface is inside a sphere (u_jpeg_mask, strength of the sphere); the
// masked shakalizer chain "HitJpeg" codes only the MCUs under the mask (indirect dispatch) and the pass Present
// puts those blocks over the frame. The strength fades linearly over the lifetime: the spot gets cleaner
// (quality -> quality_weak) and disappears.
export namespace jpeg_hits
{
    constexpr uint32_t max_hits = 64;

    // radius / lifetime <= 0: render.jpeg.hits.radius / lifetime. When full, the weakest hit is replaced.
    void add(const glm::vec3& center, float radius = 0.0f, float strength = 1.0f, float lifetime = 0.0f);
    void clear();

    // once a frame: ages the hits and drops the dead ones
    void tick(float delta_seconds);
    bool any();
    JpegHitsUBO build_ubo();

    // the settings of the chain HitJpeg
    JpegSettings chain_settings();

    // render.jpeg.hits.shoot asks for a hit where the camera looks: the render graph casts the ray
    struct ShootRequest
    {
        float radius = 0.0f;
        float strength = 1.0f;
        float lifetime = 0.0f;
    };
    std::optional<ShootRequest> take_shoot_request();
}
