module audio;

import std.compat;

import :types;
import :backend;

// Plays nothing: dedicated servers, tests, machines without an output device. Keeps the handles consistent so
// the game behaves the same: looping voices live until stopped, the others finish at the next update.
namespace
{
    using namespace audio;

    class NullBackend final : public Backend
    {
    public:
        BackendInfo get_info() const override
        {
            return { .backend = "null", .device = "none" };
        }

        Stats get_stats() const override
        {
            Stats stats;
            stats.sounds = uint32_t(std::ranges::count(sounds, true));
            stats.buses = bus_count;
            for (const Voice& voice : voices)
                stats.voices += voice.alive ? 1u : 0u;
            return stats;
        }

        void update() override
        {
            for (Voice& voice : voices)
                if (voice.alive && !voice.looping)
                    voice.alive = false;
        }

        BusId create_bus(std::string_view, BusId) override { return BusId{ bus_count++ }; }
        void set_bus_volume(BusId, float, float) override {}

        SoundId load_sound(const std::filesystem::path&, const LoadDesc&) override
        {
            sounds.push_back(true);
            return SoundId{ uint32_t(sounds.size() - 1) };
        }

        void release_sound(SoundId sound) override
        {
            if (sound.value < sounds.size())
                sounds[sound.value] = false;
        }

        SoundState get_sound_state(SoundId sound) const override
        {
            return sound.value < sounds.size() && sounds[sound.value] ? SoundState::ready : SoundState::failed;
        }

        float get_sound_length(SoundId) const override { return 0.0f; }

        VoiceId play(SoundId sound, const PlayParams& params) override
        {
            if (get_sound_state(sound) != SoundState::ready)
                return {};
            uint32_t index = 0;
            while (index < voices.size() && voices[index].alive)
                ++index;
            if (index == voices.size())
                voices.emplace_back();
            Voice& voice = voices[index];
            voice.alive = true;
            voice.looping = params.looping;
            ++voice.generation;
            return { index, voice.generation };
        }

        void stop(VoiceId voice, float) override
        {
            if (Voice* v = find(voice))
                v->alive = false;
        }

        void stop_all() override
        {
            for (Voice& voice : voices)
                voice.alive = false;
        }

        bool is_alive(VoiceId voice) const override { return const_cast<NullBackend*>(this)->find(voice) != nullptr; }
        void set_volume(VoiceId, float) override {}
        void set_pitch(VoiceId, float) override {}
        void set_spatial(VoiceId, const Spatial&) override {}
        float get_cursor(VoiceId) const override { return 0.0f; }
        void set_listener(const Listener&) override {}

    private:
        struct Voice
        {
            uint32_t generation = 0;
            bool alive = false;
            bool looping = false;
        };

        Voice* find(VoiceId id)
        {
            if (id.index >= voices.size())
                return nullptr;
            Voice& voice = voices[id.index];
            return voice.alive && voice.generation == id.generation ? &voice : nullptr;
        }

        std::vector<bool> sounds;
        std::vector<Voice> voices;
        uint32_t bus_count = 1;     // master
    };
}

namespace audio
{
    std::unique_ptr<Backend> create_null_backend(std::string&)
    {
        return std::make_unique<NullBackend>();
    }
}
