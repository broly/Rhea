module game;

import :screen_jpeg;

import std.compat;

import cvar;
import :jpeg;

namespace
{
    float g_health = 1.0f;
    float g_burst = 0.0f;
    uint32_t g_hits = 0;

    cvar::Command cmd_hit("render.jpeg.screen.hit", "Simulates a hit for the full screen JPEG damage",
        [] (cvar::Args args)
        {
            float strength = 1.0f;
            if (!args.empty())
                std::from_chars(args[0].data(), args[0].data() + args[0].size(), strength);
            screen_jpeg::hit(strength);
        },
        "[strength 0..1]");
}

void screen_jpeg::set_health(float health)
{
    g_health = std::clamp(health, 0.0f, 1.0f);
}

void screen_jpeg::hit(float strength)
{
    const float burst = std::clamp(strength, 0.0f, 1.0f) * cv_screen_jpeg_hit_strength.get();
    g_burst = 1.0f - (1.0f - g_burst) * (1.0f - burst);
    ++g_hits;
}

void screen_jpeg::tick(float delta_seconds)
{
    const float decay = std::max(cv_screen_jpeg_hit_decay.get(), 0.01f);
    g_burst *= std::exp(-std::clamp(delta_seconds, 0.0f, 0.25f) / decay);
    if (g_burst < 0.001f)
        g_burst = 0.0f;
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
    const float total = 1.0f - (1.0f - severity) * (1.0f - g_burst);

    state.severity = total;
    state.active = total > 0.001f;
    if (!state.active)
        return state;

    // fades in over the first few percent: the step from the clean frame to quality_start is not a pop
    state.amount = std::clamp(total / 0.05f, 0.0f, 1.0f);

    auto lerp_int = [total] (int from, int to)
    {
        return (int)std::lround(std::lerp((float)from, (float)to, total));
    };
    state.settings = jpeg::clamped({
        .downscale = lerp_int(1, cv_screen_jpeg_downscale_max.get()),
        .filter = cv_screen_jpeg_filter.get(),
        .quality = lerp_int(cv_screen_jpeg_quality_start.get(), cv_screen_jpeg_quality_min.get()),
        .chroma = cv_screen_jpeg_chroma.get(),
        .generations = lerp_int(1, cv_screen_jpeg_generations_max.get()),
        .sharpen = cv_screen_jpeg_sharpen.get(),
        .seed = g_hits,
    });
    return state;
}
