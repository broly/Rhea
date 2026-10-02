module game;

import :hud;

import std.compat;
import cvar;

namespace
{
    bool g_crosshair_visible = false;

    bool g_has_player = false;
    float g_health = 1.0f;
    float g_lag = 1.0f;
    float g_lag_hold = 0.0f;
    float g_flash = 0.0f;

    // time constant of the hit flash, s
    constexpr float flash_decay = 0.12f;
}

void hud::set_crosshair_visible(bool visible)
{
    g_crosshair_visible = visible;
}

float hud::crosshair_size()
{
    return g_crosshair_visible && cv_hud_crosshair.get() ? std::max(cv_hud_crosshair_size.get(), 1.0f) : 0.0f;
}

void hud::set_player_health(float fraction, bool has_player)
{
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (!g_has_player && has_player)
        g_lag = fraction;
    g_has_player = has_player;
    // a loss holds the lagging edge where it was; healing pulls it along
    if (fraction < g_health)
        g_lag_hold = cv_hud_health_lag_delay.get();
    g_health = fraction;
    g_lag = std::max(g_lag, g_health);
}

void hud::player_hit()
{
    g_flash = 1.0f;
}

void hud::tick(float delta_seconds)
{
    const float dt = std::clamp(delta_seconds, 0.0f, 0.25f);
    g_flash *= std::exp(-dt / flash_decay);
    if (g_flash < 0.01f)
        g_flash = 0.0f;
    if (g_lag_hold > 0.0f)
        g_lag_hold -= dt;
    else
        g_lag = std::max(g_lag - cv_hud_health_lag_speed.get() * dt, g_health);
}

hud::HealthBar hud::health_bar()
{
    HealthBar bar{ .health = g_health, .lag = g_lag, .flash = g_flash };
    if (g_crosshair_visible && g_has_player && cv_hud_health.get())
        bar.scale = cv_hud_health_scale.get();
    return bar;
}
