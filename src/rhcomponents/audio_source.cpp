module rhcomponents;

import :audio;
import :render_sync;

import std.compat;
import ecs;
import rhmath;
import framework;
import physics;
import audio;
import cvar;
import glm;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    cvar::Var<bool> cv_occlusion("audio.occlusion", true, "Sounds behind walls (the static world between them and the listener) are muffled");

    // beyond max_distance x this a playing source stops: a margin so it does not restart at every step
    constexpr float stop_margin = 1.1f;
    // fully occluded: this much of the volume left, low-pass at this cutoff (Hz); free: no filter above
    constexpr float occluded_volume = 0.35f;
    constexpr float occluded_lowpass = 700.0f;
    constexpr float open_lowpass = 18000.0f;
    // s, how fast a source's occlusion follows the line of sight (walking through a door)
    constexpr float occlusion_time = 0.15f;

    float random_pitch(const AudioSource& source)
    {
        static std::minstd_rand rng(0x5eed);
        if (source.pitch_variation <= 0.0f)
            return source.pitch;
        std::uniform_real_distribution<float> variation(-source.pitch_variation, source.pitch_variation);
        return std::max(source.pitch * (1.0f + variation(rng)), 0.01f);
    }

    audio::Spatial spatial_of(const AudioSource& source, const glm::vec3& position)
    {
        audio::Spatial spatial;
        spatial.position = position;
        spatial.min_distance = std::max(source.min_distance, 0.01f);
        spatial.max_distance = std::max(source.max_distance, spatial.min_distance);
        spatial.rolloff = source.rolloff;
        return spatial;
    }

    // starts the sources the listener comes near, moves the playing ones with their entity and muffles them by
    // what stands between, stops the far ones
    [[=ecs::system<ecs::Phase::Late>, =ecs::in_set<AudioSources>, =ecs::after<AudioListenerSync>,
      =ecs::after<scene::TransformPropagation>, =ecs::after<MeshColliderSync>]]
    void play_audio_sources(ecs::Query<AudioSource, const WorldTransform> sources, ecs::ResMut<audio::AudioEngine> audio,
        ecs::Res<phys::PhysicsScene> physics, ecs::Res<ecs::FrameTime> time)
    {
        const glm::vec3 listener = audio->get_listener().position;
        const float follow = 1.0f - std::exp(-float(time->dt) / occlusion_time);
        sources.each([&] (AudioSource& source, const WorldTransform& world) {
            if (source.sound.empty())
                return;
            const glm::vec3 position = world.value.position.glm();
            const float distance = glm::distance(position, listener);
            const bool playing = source.voice.is_valid() && audio->is_alive(source.voice);

            if (playing)
            {
                if (source.looping && distance > source.max_distance * stop_margin)
                {
                    audio->stop(source.voice, source.fade);
                    source.voice = {};
                    return;
                }
                audio->set_spatial(source.voice, spatial_of(source, position));
                source.occlusion += (sound_occlusion::trace(*physics, listener, position) - source.occlusion) * follow;
                const sound_occlusion::Muffle muffle = sound_occlusion::muffle(source.occlusion);
                audio->set_volume(source.voice, source.volume * muffle.volume);
                audio->set_lowpass(source.voice, muffle.lowpass);
                return;
            }
            source.voice = {};
            if (distance > source.max_distance || (!source.looping && source.played))
                return;

            source.occlusion = sound_occlusion::trace(*physics, listener, position);
            const sound_occlusion::Muffle muffle = sound_occlusion::muffle(source.occlusion);
            const audio::SoundId sound = audio->get_sound(source.sound, { .stream = source.stream });
            source.voice = audio->play(sound, {
                .volume = source.volume * muffle.volume,
                .pitch = random_pitch(source),
                .looping = source.looping,
                .spatial = spatial_of(source, position),
                .fade_in = source.looping ? source.fade : 0.0f,
                .lowpass = muffle.lowpass,
            });
            source.played = true;
        });
    }

    [[=ecs::on_remove]]
    void stop_audio_source(ecs::Registry& registry, ecs::Entity, AudioSource& source)
    {
        if (!source.voice.is_valid())
            return;
        if (audio::AudioEngine* audio = registry.find_resource<audio::AudioEngine>())
            audio->stop(source.voice, source.fade);
        source.voice = {};
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(AudioSource)
}

float sound_occlusion::trace(const phys::PhysicsScene& physics, const glm::vec3& listener, const glm::vec3& source)
{
    constexpr float skipped_end = 0.5f;
    const glm::vec3 to_source = source - listener;
    const float distance = glm::length(to_source);
    if (!cv_occlusion.get() || distance <= skipped_end + 0.05f)
        return 0.0f;
    phys::QueryFilter filter;
    filter.categories = phys::Category::static_world | phys::Category::voxel;
    return physics.raycast(listener, to_source / distance, distance - skipped_end, filter) ? 1.0f : 0.0f;
}

sound_occlusion::Muffle sound_occlusion::muffle(float occlusion)
{
    occlusion = std::clamp(occlusion, 0.0f, 1.0f);
    if (occlusion < 0.01f)
        return {};
    // the cutoff falls evenly in octaves
    const float lowpass = std::exp(glm::mix(std::log(open_lowpass), std::log(occluded_lowpass), occlusion));
    return { glm::mix(1.0f, occluded_volume, occlusion), lowpass };
}
