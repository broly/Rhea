module;

// miniaudio backend of the audio module. miniaudio is only included here (global module fragment), so none of
// its types reaches a BMI: another backend is another file like this one (see audio:backend).
#include <miniaudio.h>

module audio;

import std.compat;
import glm;
import log;
import fixed_string;

import :types;
import :backend;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogMiniaudio, Log);

namespace
{
    using namespace audio;

    ma_attenuation_model to_ma(Attenuation attenuation)
    {
        switch (attenuation)
        {
        case Attenuation::none: return ma_attenuation_model_none;
        case Attenuation::inverse: return ma_attenuation_model_inverse;
        case Attenuation::linear: return ma_attenuation_model_linear;
        case Attenuation::exponential: return ma_attenuation_model_exponential;
        }
        return ma_attenuation_model_inverse;
    }

    ma_uint64 to_ms(float seconds)
    {
        return ma_uint64(std::max(seconds, 0.0f) * 1000.0f + 0.5f);
    }

    // miniaudio logs from any thread, the audio thread included: only the problems
    void on_log(void*, ma_uint32 level, const char* message)
    {
        std::string_view text = message;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.remove_suffix(1);
        if (level == MA_LOG_LEVEL_ERROR)
            LogMiniaudio.Log<Error>("miniaudio: %.*s", int(text.size()), text.data());
        else if (level == MA_LOG_LEVEL_WARNING)
            LogMiniaudio.Log<Warning>("miniaudio: %.*s", int(text.size()), text.data());
    }

    class MiniaudioBackend final : public Backend
    {
    public:
        // miniaudio objects keep pointers to each other: the backend never moves (heap, unique_ptr)
        MiniaudioBackend() = default;
        MiniaudioBackend(const MiniaudioBackend&) = delete;
        MiniaudioBackend& operator=(const MiniaudioBackend&) = delete;

        bool init(std::string& error)
        {
            ma_engine_config config = ma_engine_config_init();
            config.listenerCount = 1;
            if (const ma_result result = ma_engine_init(&config, &engine); result != MA_SUCCESS)
            {
                error = ma_result_description(result);
                return false;
            }
            initialized = true;

            ma_log_register_callback(ma_engine_get_log(&engine), ma_log_callback_init(&on_log, nullptr));

            // the master bus: everything goes through it to the device
            auto master = std::make_unique<ma_sound_group>();
            if (const ma_result result = ma_sound_group_init(&engine, 0, nullptr, master.get()); result != MA_SUCCESS)
            {
                error = ma_result_description(result);
                return false;
            }
            buses.push_back(std::move(master));
            return true;
        }

        ~MiniaudioBackend() override
        {
            if (!initialized)
                return;
            // voices before the sounds whose data they read, groups after the voices attached to them
            for (Voice& voice : voices)
                if (voice.state != VoiceState::free)
                    ma_sound_uninit(voice.sound.get());
            for (Sound& sound : sounds)
                release(sound);
            for (auto it = buses.rbegin(); it != buses.rend(); ++it)
                ma_sound_group_uninit(it->get());
            ma_engine_uninit(&engine);
        }

        BackendInfo get_info() const override
        {
            ma_engine* e = const_cast<ma_engine*>(&engine);
            BackendInfo info;
            info.backend = "miniaudio";
            if (const ma_device* device = ma_engine_get_device(e))
                info.device = device->playback.name;
            info.sample_rate = ma_engine_get_sample_rate(e);
            info.channels = ma_engine_get_channels(e);
            return info;
        }

        Stats get_stats() const override
        {
            Stats stats;
            stats.buses = uint32_t(buses.size());
            for (uint32_t i = 0; i < sounds.size(); ++i)
            {
                if (!sounds[i].used)
                    continue;
                ++stats.sounds;
                if (get_sound_state(SoundId{ i }) == SoundState::loading)
                    ++stats.sounds_loading;
            }
            for (const Voice& voice : voices)
                stats.voices += voice.state != VoiceState::free ? 1u : 0u;
            return stats;
        }

        void update() override
        {
            const ma_uint64 now = ma_engine_get_time_in_pcm_frames(&engine);
            for (Voice& voice : voices)
            {
                if (voice.state == VoiceState::free)
                    continue;
                // waiting for its delay: not started yet, not finished either
                if (now < voice.start_frame)
                    continue;
                if (!ma_sound_is_playing(voice.sound.get()))
                    free_voice(voice);
            }
        }

        // --- buses ---

        BusId create_bus(std::string_view, BusId parent) override
        {
            ma_sound_group* parent_group = find_bus(parent);
            if (!parent_group)
                parent_group = buses.front().get();
            auto group = std::make_unique<ma_sound_group>();
            if (const ma_result result = ma_sound_group_init(&engine, 0, parent_group, group.get()); result != MA_SUCCESS)
            {
                LogMiniaudio.Log<Error>("Bus not created: %s", ma_result_description(result));
                return {};
            }
            buses.push_back(std::move(group));
            return BusId{ uint32_t(buses.size() - 1) };
        }

        void set_bus_volume(BusId bus, float volume, float fade) override
        {
            // the bus volume is its fader: a fade starts from wherever the previous one is
            if (ma_sound_group* group = find_bus(bus))
                ma_sound_group_set_fade_in_milliseconds(group, -1.0f, volume, to_ms(fade));
        }

        // --- sounds ---

        SoundId load_sound(const std::filesystem::path& file, const LoadDesc& desc) override
        {
            uint32_t index = 0;
            while (index < sounds.size() && sounds[index].used)
                ++index;
            if (index == sounds.size())
                sounds.emplace_back();

            Sound& sound = sounds[index];
            sound.used = true;
            sound.file = file;
            sound.stream = desc.stream;

            if (!desc.stream)
            {
                // decoded in the background by the resource manager; voices are copies sharing its data. Not
                // attached to anything, it is never played itself
                sound.prototype = std::make_unique<ma_sound>();
                const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
                if (const ma_result result = ma_sound_init_from_file_w(&engine, file.c_str(), flags, nullptr, nullptr,
                        sound.prototype.get()); result != MA_SUCCESS)
                {
                    LogMiniaudio.Log<Warning>("Sound %s not loaded: %s", file.string().c_str(), ma_result_description(result));
                    sound.prototype.reset();
                    sound.failed = true;
                }
            }
            return SoundId{ index };
        }

        void release_sound(SoundId id) override
        {
            if (id.value < sounds.size() && sounds[id.value].used)
                release(sounds[id.value]);
        }

        SoundState get_sound_state(SoundId id) const override
        {
            const Sound* sound = find_sound(id);
            if (!sound || sound->failed)
                return SoundState::failed;
            if (!sound->prototype)
                return SoundState::ready;           // a stream opens its file when a voice plays it
            const auto* source = static_cast<const ma_resource_manager_data_source*>(
                ma_sound_get_data_source(sound->prototype.get()));
            switch (ma_resource_manager_data_source_result(source))
            {
            case MA_SUCCESS: return SoundState::ready;
            case MA_BUSY: return SoundState::loading;
            default: return SoundState::failed;
            }
        }

        float get_sound_length(SoundId id) const override
        {
            const Sound* sound = find_sound(id);
            if (!sound || !sound->prototype || get_sound_state(id) != SoundState::ready)
                return 0.0f;
            float length = 0.0f;
            ma_sound_get_length_in_seconds(sound->prototype.get(), &length);
            return length;
        }

        // --- voices ---

        VoiceId play(SoundId id, const PlayParams& params) override
        {
            const Sound* sound = find_sound(id);
            if (!sound || get_sound_state(id) == SoundState::failed)
                return {};

            ma_sound_group* group = find_bus(params.bus);
            if (!group)
                group = buses.front().get();

            uint32_t index = 0;
            while (index < voices.size() && voices[index].state != VoiceState::free)
                ++index;
            if (index == voices.size())
                voices.emplace_back();
            Voice& voice = voices[index];
            if (!voice.sound)
                voice.sound = std::make_unique<ma_sound>();

            ma_uint32 flags = params.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
            ma_result result;
            if (sound->prototype)
                result = ma_sound_init_copy(&engine, sound->prototype.get(), flags, group, voice.sound.get());
            else
            {
                flags |= MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC;
                result = ma_sound_init_from_file_w(&engine, sound->file.c_str(), flags, group, nullptr, voice.sound.get());
            }
            if (result != MA_SUCCESS)
            {
                LogMiniaudio.Log<Warning>("Sound %s not played: %s", sound->file.string().c_str(), ma_result_description(result));
                return {};
            }

            ma_sound* s = voice.sound.get();
            ma_sound_set_volume(s, params.volume);
            ma_sound_set_pitch(s, params.pitch);
            ma_sound_set_looping(s, params.looping ? MA_TRUE : MA_FALSE);
            if (params.spatial)
                apply_spatial(s, *params.spatial);
            if (params.fade_in > 0.0f)
                ma_sound_set_fade_in_milliseconds(s, 0.0f, 1.0f, to_ms(params.fade_in));

            voice.start_frame = ma_engine_get_time_in_pcm_frames(&engine);
            if (params.delay > 0.0f)
            {
                voice.start_frame += ma_uint64(params.delay * float(ma_engine_get_sample_rate(&engine)));
                ma_sound_set_start_time_in_pcm_frames(s, voice.start_frame);
            }
            ma_sound_start(s);

            voice.state = VoiceState::playing;
            voice.spatial = params.spatial.has_value();
            ++voice.generation;
            return { index, voice.generation };
        }

        void stop(VoiceId id, float fade) override
        {
            Voice* voice = find_voice(id);
            if (!voice)
                return;
            if (fade <= 0.0f)
            {
                free_voice(*voice);
                return;
            }
            // recycled by update() when the fade is over
            ma_sound_stop_with_fade_in_milliseconds(voice->sound.get(), to_ms(fade));
            voice->state = VoiceState::stopping;
        }

        void stop_all() override
        {
            for (Voice& voice : voices)
                if (voice.state != VoiceState::free)
                    free_voice(voice);
        }

        bool is_alive(VoiceId id) const override
        {
            return const_cast<MiniaudioBackend*>(this)->find_voice(id) != nullptr;
        }

        void set_volume(VoiceId id, float volume) override
        {
            if (Voice* voice = find_voice(id))
                ma_sound_set_volume(voice->sound.get(), volume);
        }

        void set_pitch(VoiceId id, float pitch) override
        {
            if (Voice* voice = find_voice(id))
                ma_sound_set_pitch(voice->sound.get(), pitch);
        }

        void set_spatial(VoiceId id, const Spatial& spatial) override
        {
            if (Voice* voice = find_voice(id); voice && voice->spatial)
                apply_spatial(voice->sound.get(), spatial);
        }

        float get_cursor(VoiceId id) const override
        {
            Voice* voice = const_cast<MiniaudioBackend*>(this)->find_voice(id);
            if (!voice)
                return 0.0f;
            float cursor = 0.0f;
            ma_sound_get_cursor_in_seconds(voice->sound.get(), &cursor);
            return cursor;
        }

        void set_listener(const Listener& listener) override
        {
            ma_engine_listener_set_position(&engine, 0, listener.position.x, listener.position.y, listener.position.z);
            ma_engine_listener_set_direction(&engine, 0, listener.forward.x, listener.forward.y, listener.forward.z);
            ma_engine_listener_set_world_up(&engine, 0, listener.up.x, listener.up.y, listener.up.z);
            ma_engine_listener_set_velocity(&engine, 0, listener.velocity.x, listener.velocity.y, listener.velocity.z);
        }

    private:
        struct Sound
        {
            bool used = false;
            bool stream = false;
            bool failed = false;
            std::filesystem::path file;
            std::unique_ptr<ma_sound> prototype;     // decoded sounds: holds the data in the resource manager
        };

        enum class VoiceState : uint8_t
        {
            free,
            playing,
            stopping,       // fading out, no longer controllable
        };

        struct Voice
        {
            std::unique_ptr<ma_sound> sound;         // kept while free: no allocation per play
            uint32_t generation = 0;
            VoiceState state = VoiceState::free;
            bool spatial = false;
            ma_uint64 start_frame = 0;               // engine time it begins at (delay)
        };

        static void apply_spatial(ma_sound* s, const Spatial& spatial)
        {
            ma_sound_set_position(s, spatial.position.x, spatial.position.y, spatial.position.z);
            ma_sound_set_velocity(s, spatial.velocity.x, spatial.velocity.y, spatial.velocity.z);
            ma_sound_set_min_distance(s, spatial.min_distance);
            ma_sound_set_max_distance(s, spatial.max_distance);
            ma_sound_set_rolloff(s, spatial.rolloff);
            ma_sound_set_attenuation_model(s, to_ma(spatial.attenuation));
            ma_sound_set_doppler_factor(s, spatial.doppler);
        }

        void release(Sound& sound)
        {
            if (sound.prototype)
                ma_sound_uninit(sound.prototype.get());
            sound = Sound{};
        }

        void free_voice(Voice& voice)
        {
            ma_sound_uninit(voice.sound.get());
            voice.state = VoiceState::free;
        }

        ma_sound_group* find_bus(BusId bus) const
        {
            return bus.value < buses.size() ? buses[bus.value].get() : nullptr;
        }

        const Sound* find_sound(SoundId id) const
        {
            return id.value < sounds.size() && sounds[id.value].used ? &sounds[id.value] : nullptr;
        }

        Voice* find_voice(VoiceId id)
        {
            if (id.index >= voices.size())
                return nullptr;
            Voice& voice = voices[id.index];
            return voice.state == VoiceState::playing && voice.generation == id.generation ? &voice : nullptr;
        }

        ma_engine engine{};
        bool initialized = false;
        std::vector<std::unique_ptr<ma_sound_group>> buses;     // [0]: master
        std::vector<Sound> sounds;
        std::vector<Voice> voices;
    };
}

namespace audio
{
    std::unique_ptr<Backend> create_miniaudio_backend(std::string& error)
    {
        auto backend = std::make_unique<MiniaudioBackend>();
        if (!backend->init(error))
            return nullptr;
        return backend;
    }
}
