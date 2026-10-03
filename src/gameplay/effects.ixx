module;

#include <json/value.h>

export module gameplay:effects;

import std.compat;
import glm;
import reflect;
import rhobject;

// One shot effects (muzzle flashes, tracers, impacts): prefabs with a ParticleSystem that remove themselves.
//
//     "Lifetime": { "seconds": 0.4 }
export struct Lifetime
{
    [[=rh::edit, =rh::speed<0.01f>]] float seconds = 1.0f;
    [[=rh::edit, =rh::read_only]] float age = 0.0f;
};

// A JPEG spot for a moment where game rules say (a bite at a jackal's muzzle), besides the spots of the blows
// (DamageEvent::jpeg_preset). The game's damage feedback hands it to the JPEG hits (game:jpeg_hits).
// preset: assets/jpeg/presets.json, section "hits"; radius / lifetime <= 0: the preset's.
export struct JpegSpotEvent
{
    glm::vec3 position{ 0.0f };
    std::string preset = "default";
    float strength = 1.0f;
    float radius = 0.0f;
    float lifetime = 0.0f;
};

// Whole bodies under the JPEG of a preset (game:jpeg_hits::hold_body) for `lifetime` s: keeps the preset's chain
// running and gives the bodies their mask channel. The bodies mark themselves (MeshRenderer::effect.a). When bodies
// of several presets are marked at once, all of them take the strongest one's: strength < 1 lets a weapon's
// body JPEG (DamageEvent::body_jpeg_time, strength 1) win over a background one while it is fresh.
export struct JpegBodyEvent
{
    std::string preset = "default";
    float lifetime = 0.2f;
    float strength = 1.0f;
};

// An infected creature: now and then its body pulses with JPEG blocks (MeshRenderer::effect.a of it and its
// children under the body JPEG of `preset`), and once it is dead it stays crushed until it is gone. Levels are the
// g-buffer's 4 steps (0, 1/3, 2/3, 1): a pulse rises and falls through them, the preset's quality goes from
// quality_weak to quality with the level. A blow's body JPEG (HitFlash) on top takes the stronger level.
//
//     "JpegInfection": { "preset": "infection", "pulse_strength": 0.67, "dead_strength": 1.0 }
export struct JpegInfection
{
    [[=rh::edit]] std::string preset = "infection";                    // assets/jpeg/presets.json, section "hits"
    [[=rh::edit, =rh::speed<0.05f>]] float interval_min = 2.0f;         // s between two pulses
    [[=rh::edit, =rh::speed<0.05f>]] float interval_max = 6.0f;
    [[=rh::edit, =rh::speed<0.01f>]] float pulse_time = 0.6f;           // s a pulse lasts
    [[=rh::edit, =rh::speed<0.01f>]] float pulse_strength = 0.67f;      // level at the top of a pulse
    [[=rh::edit, =rh::speed<0.01f>]] float dead_strength = 1.0f;        // level of the dead body
    [[=rh::edit, =rh::speed<0.01f>]] float dead_fade_in = 0.3f;         // s from the death to dead_strength

    [[=rh::edit, =rh::read_only, =rh::transient]] float level = 0.0f;
    [[=rh::transient]] float next_pulse = 0.0f;     // s to the next pulse
    [[=rh::transient]] float pulse_age = -1.0f;     // s into the pulse; < 0: none
    [[=rh::transient]] int applied_step = -1;       // the step written into the MeshRenderers
    [[=rh::transient]] bool initialized = false;
    [[=rh::transient]] std::minstd_rand rng;
};

// System set of the Late system that writes the infection levels into the MeshRenderers (after HitFlashes)
export struct JpegInfections {};
