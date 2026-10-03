export module gameplay:sounds;

import std.compat;
import reflect;
import rhobject;
import audio;

// What the game sounds like (S2): gameplay state and events -> audio::AudioEngine (the sfx bus).
//
//   play_weapon_sounds  (Update)  WeaponFiredEvent, WeaponImpactEvent, WeaponActionEvent -> the weapon's sounds
//                                 (WeaponDef::sounds, assets/weapons/weapons.json); a beam's loop while its rays go.
//                                 The player's own weapon plays flat (2D), anyone else's in the world.
//   play_footsteps      (Update, after anim::Evaluate)  the footstep notifies of the clips (ALS FootstepEffects:
//                                 foot, step / walk-run / land) of locomotion characters -> a step where the
//                                 character is: audio/environment/foley/footstep_grass_*.wav on the terrain,
//                                 footstep_stone_*.wav on anything else (a ray down finds the ground)
//   play_ambience       (Update)  OutdoorAmbience: the day and night beds crossfaded by the sun's elevation
//                                 (SkyController), calls at random around the listener at night. Indoors they
//                                 are muffled: rays from the listener up and around measure how open the place is
//
// Sounds in the world that the level hides from the listener (behind a wall, outside while the listener is in)
// are muffled: rhcomponents sound_occlusion (cvar audio.occlusion). The player's own weapon and steps are not.
//
// cvars: audio.footsteps.volume, audio.footsteps.debug
export
{
    // The outdoor sound bed of a level, on one entity (the sky): looping streams around the listener, the day
    // ones above day_elevation of the sun, the night ones below night_elevation, crossfaded between. Without a
    // SkyController in the world it is always day.
    //
    //     "OutdoorAmbience": { "day": "audio/environment/ambience/day_valley.ogg", "volume": 0.5 }
    struct OutdoorAmbience
    {
        [[=rh::edit]] std::string day = "audio/environment/ambience/day_valley.ogg";
        [[=rh::edit]] std::string night = "audio/environment/ambience/night_meadow.ogg";
        // a second night layer under the first one (the crickets close by)
        [[=rh::edit]] std::string night_layer = "audio/environment/ambience/night_crickets.ogg";
        [[=rh::edit, =rh::speed<0.01f>]] float volume = 0.5f;
        [[=rh::edit, =rh::speed<0.01f>]] float night_layer_volume = 0.5f;
        // degrees of the sun above the horizon: all night below night_elevation, all day above day_elevation
        [[=rh::edit, =rh::speed<0.1f>]] float night_elevation = -8.0f;
        [[=rh::edit, =rh::speed<0.1f>]] float day_elevation = 4.0f;
        // at night, now and then, somewhere around the listener (an owl)
        [[=rh::edit]] std::string night_call = "audio/environment/owl_hoot.wav";
        [[=rh::edit, =rh::speed<0.01f>]] float night_call_volume = 0.6f;
        // s between two calls, min .. max
        [[=rh::edit, =rh::speed<0.5f>]] float night_call_interval_min = 25.0f;
        [[=rh::edit, =rh::speed<0.5f>]] float night_call_interval_max = 70.0f;
        // indoors (a ceiling and walls around the listener) the beds are this much quieter and low-passed at
        // indoor_lowpass Hz; in between (a courtyard, an arcade) partly
        [[=rh::edit, =rh::speed<0.01f>]] float indoor_volume = 0.3f;
        [[=rh::edit, =rh::speed<10.f>]] float indoor_lowpass = 800.0f;

        // 0 indoors .. 1 under the open sky, around the listener (smoothed)
        [[=rh::edit, =rh::read_only, =rh::transient]] float outdoor = 1.0f;
        [[=rh::transient]] audio::VoiceId day_voice;
        [[=rh::transient]] audio::VoiceId night_voice;
        [[=rh::transient]] audio::VoiceId night_layer_voice;
        [[=rh::transient]] float next_call = -1.0f;       // s until the next night call (< 0: not drawn yet)
    };
}
