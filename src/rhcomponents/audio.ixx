export module rhcomponents:audio;

import std.compat;
import glm;
import reflect;
import rhobject;
import audio;
import physics;

// A sound where the entity is (a campfire, a stream): a looping voice on the sfx bus that follows the entity,
// started when the listener comes within max_distance and faded out beyond it (and when the component goes).
// A non-looping source plays once, the first time the listener is in range.
//
//     "AudioSource": { "sound": "audio/environment/campfire_loop.ogg", "stream": true, "volume": 0.8,
//                      "min_distance": 1.5, "max_distance": 30 }
//
// The level between the listener and a source muffles it (sound_occlusion below: quieter and duller), smoothly
// as the line opens or closes.
//
// Systems (Late, after the transform propagation): sync_audio_listener (set AudioListenerSync: the active
// camera), then play_audio_sources (set AudioSources).
//
// cvars: audio.occlusion (sounds behind walls are muffled)
export struct AudioSource
{
    // path under assets/
    [[=rh::edit]] std::string sound;
    [[=rh::edit, =rh::speed<0.01f>]] float volume = 1.0f;
    [[=rh::edit, =rh::speed<0.01f>]] float pitch = 1.0f;
    // the pitch of each start is pitch +- a random share up to this (two campfires do not sound the same)
    [[=rh::edit, =rh::speed<0.005f>]] float pitch_variation = 0.0f;
    [[=rh::edit]] bool looping = true;
    // decoded while it plays instead of once into memory: long loops
    [[=rh::edit]] bool stream = false;
    // m: full volume closer than min_distance, silent (and stopped) beyond max_distance
    [[=rh::edit, =rh::speed<0.05f>]] float min_distance = 1.0f;
    [[=rh::edit, =rh::speed<0.5f>]] float max_distance = 40.0f;
    [[=rh::edit, =rh::speed<0.01f>]] float rolloff = 1.0f;
    // s, the fade in when it starts and out when it stops
    [[=rh::edit, =rh::speed<0.01f>]] float fade = 0.5f;

    [[=rh::transient]] audio::VoiceId voice;
    [[=rh::transient]] bool played = false;
    // 0 .. 1, how much the level stands between the listener and the source (smoothed)
    [[=rh::edit, =rh::read_only, =rh::transient]] float occlusion = 0.0f;
};

// Sound through the level: walls, the terrain, voxels between the listener and a sound make it quieter and duller
export namespace sound_occlusion
{
    // 0: the line from the listener to the sound is free, 1: the static world blocks it. The last half metre to
    // the sound is not traced: its own collider (the stones around a campfire) does not hide it. Always 0 with
    // the cvar audio.occlusion off
    float trace(const phys::PhysicsScene& physics, const glm::vec3& listener, const glm::vec3& source);

    struct Muffle
    {
        float volume = 1.0f;        // factor
        float lowpass = 0.0f;       // Hz, the cutoff of PlayParams::lowpass; 0: none
    };

    // occlusion 0 .. 1 -> how the sound is muffled
    Muffle muffle(float occlusion);
}

// System set of sync_audio_listener (the ears: the active camera)
export struct AudioListenerSync {};
// System set of play_audio_sources
export struct AudioSources {};
