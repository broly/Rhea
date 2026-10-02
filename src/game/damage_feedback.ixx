export module game:damage_feedback;

import std.compat;

// What damage looks like (G3 -> J3, J4): the system jpeg_damage_feedback (Update) reads the gameplay events
//   - the health of the player (Health + Player)  -> screen_jpeg::set_health (the vignette), hud (health bar)
//   - damage the player takes (DamageTakenEvent)              -> screen_jpeg::hit (pulse, red edges), stronger for
//                                                                bigger blows; hud::player_hit (the bar flashes)
//   - any other DamageEvent with a jpeg_preset                 -> a JPEG spot where it hit (jpeg_hits)
//   - DeathEvent with a death_jpeg                             -> a JPEG spot where the entity died
// Debug commands: damage.player <amount> [preset], damage.look <amount> [preset] (the surface in the middle of
// the screen; the render graph casts the ray: it has the camera).
export namespace damage_feedback
{
    struct LookRequest
    {
        float amount = 10.0f;
        std::string preset = "default";
    };
    std::optional<LookRequest> take_look_request();
}
