export module game:hud;

import std.compat;
import cvar;

// The few things the game draws over the frame before there is a game UI (E11): the crosshair in the middle of
// the screen (where the weapons aim: the ray of the camera's center), drawn by the pass Present
// (jpeg_present.frag), so it shows with the debug UI hidden too.
export cvar::Var<bool> cv_hud_crosshair("hud.crosshair", true, "Crosshair in the middle of the screen while the player aims the weapons");
export cvar::Var<float> cv_hud_crosshair_size("hud.crosshair.size", 7.0f, "Length of a crosshair arm, px",
    { .has_range = true, .min = 2.0f, .max = 40.0f });

export namespace hud
{
    // the player controls the character (the weapons fire): the game script sets it every frame
    void set_crosshair_visible(bool visible);
    // arm length in px, 0: hidden
    float crosshair_size();
}
