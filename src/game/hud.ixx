export module game:hud;

import std.compat;
import cvar;

// The few things the game draws over the frame before there is a game UI (E11), drawn by the pass Present
// (jpeg_present.frag), so they show with the debug UI hidden too:
//   - the crosshair in the middle of the screen (where the weapons aim: the ray of the camera's center)
//   - the health bar in the bottom left corner: the health, the chunk just lost (holds, then drains) and a flash
//     on hits
// Both only while the player controls the character (the game script: set_crosshair_visible).
export cvar::Var<bool> cv_hud_crosshair("hud.crosshair", true, "Crosshair in the middle of the screen while the player aims the weapons");
export cvar::Var<float> cv_hud_crosshair_size("hud.crosshair.size", 7.0f, "Length of a crosshair arm, px",
    { .has_range = true, .min = 2.0f, .max = 40.0f });
export cvar::Var<bool> cv_hud_health("hud.health", true, "Health bar in the bottom left corner while the player controls the character");
export cvar::Var<float> cv_hud_health_scale("hud.health.scale", 1.0f, "Size of the health bar (1: 260 x 12 px at 1080p)",
    { .has_range = true, .min = 0.25f, .max = 4.0f });
export cvar::Var<float> cv_hud_health_lag_delay("hud.health.lag_delay", 0.6f, "Seconds the lost chunk of the bar holds before it drains",
    { .has_range = true, .min = 0.0f, .max = 5.0f });
export cvar::Var<float> cv_hud_health_lag_speed("hud.health.lag_speed", 0.6f, "How fast the lost chunk drains, health per second",
    { .has_range = true, .min = 0.05f, .max = 10.0f });

export namespace hud
{
    // the player controls the character (the weapons fire): the game script sets it every frame
    void set_crosshair_visible(bool visible);
    // arm length in px, 0: hidden
    float crosshair_size();

    // the player's health, 0..1, once a frame (has_player: there is a player with a Health)
    void set_player_health(float fraction, bool has_player);
    // a hit on the player: the bar flashes
    void player_hit();
    void tick(float delta_seconds);

    struct HealthBar
    {
        float health = 1.0f;
        // the lagging edge of the lost chunk
        float lag = 1.0f;
        float flash = 0.0f;
        // 0: hidden
        float scale = 0.0f;
    };
    HealthBar health_bar();
}
