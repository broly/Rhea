module game;

import :hud;

import std.compat;
import cvar;

namespace
{
    bool g_crosshair_visible = false;
}

void hud::set_crosshair_visible(bool visible)
{
    g_crosshair_visible = visible;
}

float hud::crosshair_size()
{
    return g_crosshair_visible && cv_hud_crosshair.get() ? std::max(cv_hud_crosshair_size.get(), 1.0f) : 0.0f;
}
