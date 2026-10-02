module game;

import :screen_jpeg;

import std.compat;
import glm;

import cvar;
import :jpeg;

namespace
{
    float g_health = 1.0f;
    // the JPEG pulse and the red edges of the hits, 0..1
    float g_pulse = 0.0f;
    float g_red = 0.0f;
    uint32_t g_hits = 0;
    uint32_t g_frame = 0;

    // the pulse above which the grid and the dither jump every frame (hit.flicker)
    constexpr float flicker_pulse = 0.15f;

    cvar::Command cmd_hit("render.jpeg.screen.hit", "Simulates a hit for the full screen JPEG damage",
        [] (cvar::Args args)
        {
            float strength = 1.0f;
            if (!args.empty())
                std::from_chars(args[0].data(), args[0].data() + args[0].size(), strength);
            screen_jpeg::hit(strength);
        },
        "[strength 0..1]");

    float decay(float value, float delta_seconds, float time_constant)
    {
        value *= std::exp(-std::clamp(delta_seconds, 0.0f, 0.25f) / std::max(time_constant, 0.01f));
        return value < 0.001f ? 0.0f : value;
    }

    int lerp_int(int from, int to, float t)
    {
        return (int)std::lround(std::lerp((float)from, (float)to, std::clamp(t, 0.0f, 1.0f)));
    }
}

void screen_jpeg::set_health(float health)
{
    g_health = std::clamp(health, 0.0f, 1.0f);
}

void screen_jpeg::hit(float strength)
{
    const float s = std::clamp(strength, 0.0f, 1.0f) * cv_screen_jpeg_hit_strength.get();
    g_pulse = 1.0f - (1.0f - g_pulse) * (1.0f - s);
    g_red = 1.0f - (1.0f - g_red) * (1.0f - s);
    ++g_hits;
}

void screen_jpeg::tick(float delta_seconds)
{
    g_pulse = decay(g_pulse, delta_seconds, cv_screen_jpeg_hit_decay.get());
    g_red = decay(g_red, delta_seconds, cv_screen_jpeg_red_decay.get());
    ++g_frame;
}

screen_jpeg::State screen_jpeg::evaluate()
{
    State state;
    if (!cv_screen_jpeg_enabled.get())
        return state;

    const float debug_health = cv_screen_jpeg_debug_health.get();
    const float health = debug_health >= 0.0f ? debug_health : g_health;
    const float threshold = std::max(cv_screen_jpeg_threshold.get(), 0.01f);
    const float below = std::clamp((threshold - health) / threshold, 0.0f, 1.0f);
    const float severity = below > 0.0f ? std::pow(below, std::max(cv_screen_jpeg_curve.get(), 0.01f)) : 0.0f;
    const float pulse = g_pulse;

    state.health_severity = severity;
    state.pulse = pulse;
    state.tint = cv_screen_jpeg_red_color.get() * (g_red * cv_screen_jpeg_red.get());
    state.tint_radius = cv_screen_jpeg_red_radius.get();
    state.active = below > 0.0f || pulse > 0.0f;
    if (!state.active)
        return state;

    const bool flicker = cv_screen_jpeg_hit_flicker.get() && pulse > flicker_pulse;
    state.seed = flicker ? g_hits * 7919u + g_frame : g_hits;
    state.block = (float)std::max(cv_screen_jpeg_block.get(), 0);
    state.amount = std::clamp(pulse / std::max(cv_screen_jpeg_hit_fade.get(), 0.01f), 0.0f, 1.0f);
    if (below > 0.0f)
    {
        state.vignette_radius = std::lerp(cv_screen_jpeg_radius_start.get(), cv_screen_jpeg_radius_min.get(), severity);
        state.vignette_feather = cv_screen_jpeg_feather.get();
    }

    // each contribution's parameters; the chain takes the harsher one
    const bool hurt = below > 0.0f;
    const bool hit = pulse > 0.0f;
    const int health_downscale = lerp_int(1, cv_screen_jpeg_downscale_max.get(), severity);
    const int health_quality = lerp_int(cv_screen_jpeg_quality_start.get(), cv_screen_jpeg_quality_min.get(), severity);
    const int health_generations = lerp_int(1, cv_screen_jpeg_generations_max.get(), severity);
    const int hit_quality = lerp_int(cv_screen_jpeg_quality_start.get(), cv_screen_jpeg_hit_quality.get(), pulse);

    state.settings = jpeg::clamped({
        .downscale = std::max(hurt ? health_downscale : 1, lerp_int(1, cv_screen_jpeg_hit_downscale_y.get(), pulse)),
        .downscale_x = std::max(hurt ? health_downscale : 1, lerp_int(1, cv_screen_jpeg_hit_downscale_x.get(), pulse)),
        .filter = cv_screen_jpeg_filter.get(),
        .quality = std::min(hurt ? health_quality : 100, hit ? hit_quality : 100),
        .chroma = cv_screen_jpeg_chroma.get(),
        .generations = std::max(hurt ? health_generations : 1, lerp_int(1, cv_screen_jpeg_hit_generations.get(), pulse)),
        .sharpen = cv_screen_jpeg_sharpen.get(),
        .seed = state.seed,
        .noise = std::max(severity * cv_screen_jpeg_health_noise.get(), pulse * cv_screen_jpeg_hit_noise.get()),
    });
    return state;
}
