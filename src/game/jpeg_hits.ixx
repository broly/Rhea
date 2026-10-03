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

export cvar::Var<bool> cv_jpeg_hits_enabled(
    "render.jpeg.hits.enabled", true, "JPEG spots where hits land: the 8 x 8 blocks of the screen over the hit surfaces are compressed");

/************************************************************************
 * PRESETS: assets/jpeg/presets.json, section "hits": { "<name>": { field: value } }, reloaded when the file
 * changes. Missing fields keep the defaults below.
 *   downscale 1..16, filter "nearest" | "box", quality 1..100 (fresh hit), quality_weak (faded hit),
 *   chroma "4:4:4" | "4:2:2" | "4:2:0" | "4:1:1", generations 1..16, sharpen 0..1,
 *   radius (m), lifetime (s), edge (0..1: ragged share of the radius), description,
 *   brightness (gain before the codec: 0 none, 1 twice as bright), noise (0..1 color noise before the codec);
 *   both times the spot's strength
 ***********************************************************************/

export struct JpegHitPreset
{
    std::string name;
    std::string description;
    // quality: a fresh hit (strength 1), mask_quality: a faded one
    JpegSettings settings{ .downscale = 1, .filter = JpegFilter::Nearest, .quality = 3, .chroma = JpegChroma::Yuv420,
        .generations = 2, .sharpen = 0.3f, .mask_quality = 30 };
    float radius = 0.6f;
    float lifetime = 2.5f;
    float edge = 0.5f;
};

/************************************************************************
 * HITS
 ***********************************************************************/

// JPEG spots of the hits (J4, J6): spheres in the world with a lifetime and a preset (a weapon). Every frame
// the pass JpegHitMask marks the 8 x 8 blocks of the screen whose surface is inside a sphere (u_jpeg_mask,
// strength of the sphere in the channel of its slot); the masked shakalizer chains "HitJpeg<slot>" code only
// the MCUs under their channel (indirect dispatch) and write the blocks their channel wins; the pass Present
// puts those blocks over the frame. The strength fades linearly over the lifetime: the spot gets cleaner
// (quality -> quality_weak) and disappears.
// A slot is a chain: up to slot_count presets are drawn as they are each frame (the strongest ones), the hits of
// the others borrow the slot of the most similar preset.
export namespace jpeg_hits
{
    constexpr uint32_t max_hits = 64;
    constexpr uint32_t slot_count = 4;

    // preset: a name of assets/jpeg/presets.json (unknown: "default"); radius / lifetime <= 0: the preset's.
    // When full, the weakest hit is replaced.
    void add(const glm::vec3& center, std::string_view preset = "default", float strength = 1.0f,
        float radius = 0.0f, float lifetime = 0.0f);
    // Whole bodies under the JPEG of a preset for `lifetime` s (a creature hit by a weapon). The bodies mark
    // themselves in the g-buffer (MeshRenderer::effect.a, gameplay HitFlash); this keeps the preset's chain
    // running and gives the mask pass its channel. Bodies of several presets at once share the strongest one's
    // (strength x the share of the lifetime left: a background JPEG, the infection pulses, holds with less than 1).
    void hold_body(std::string_view preset, float lifetime, float strength = 1.0f);
    void clear();

    // once a frame: ages the hits, drops the dead ones, reloads the presets when the file changed
    void tick(float delta_seconds);

    struct Frame
    {
        bool any = false;
        JpegHitsUBO ubo{};
        std::array<bool, slot_count> slot_active{};
        std::array<JpegSettings, slot_count> slot_settings{};
    };
    // once a frame after tick: the slots of the presets and the spheres for the GPU
    const Frame& update();

    const std::vector<JpegHitPreset>& get_presets();
    const JpegHitPreset* find_preset(std::string_view name);
    bool reload_presets();
    std::filesystem::path get_presets_path();

    // render.jpeg.hits.shoot asks for a hit where the camera looks: the render graph casts the ray
    struct ShootRequest
    {
        std::string preset = "default";
        float strength = 1.0f;
        float radius = 0.0f;
        float lifetime = 0.0f;
    };
    std::optional<ShootRequest> take_shoot_request();
}
