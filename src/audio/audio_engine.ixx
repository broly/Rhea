export module audio:engine;

import std.compat;
import glm;

import :types;
import :backend;

// The engine's audio service, on top of an exchangeable Backend (audio:backend): the game's buses with their
// volume cvars, sound assets by path, the listener. Owned by the Engine, a resource of the World
// (ResMut<audio::AudioEngine>). Main thread only.
//
//     audio.play("audio/weapons/blaster.wav", { .bus = audio.get_bus(audio::Bus::sfx),
//                                              .spatial = audio::Spatial{ .position = muzzle } });
//
// cvars: audio.backend (at start), audio.volume.{master, music, sfx, voice, ui}, audio.mute.
export namespace audio
{
    // The game's buses, all under master
    enum class Bus : uint8_t
    {
        master,
        music,
        sfx,        // effects: weapons, creatures, footsteps, ambience
        voice,      // dialogue lines (ducks the music, S3)
        ui,
        count,
    };

    std::string_view get_bus_name(Bus bus);

    class AudioEngine
    {
    public:
        AudioEngine();
        ~AudioEngine();

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        // Backend by name, the cvar audio.backend when empty. Falls back to the next backend of get_backends()
        // that starts (at worst "null": silence, the game runs the same)
        void init(std::string_view backend_name = {});
        void shutdown();
        bool is_initialized() const { return backend != nullptr; }

        BackendInfo get_info() const;
        Stats get_stats() const;
        // Direct access for what the engine API does not cover
        Backend& get_backend() { return *backend; }

        BusId get_bus(Bus bus) const { return buses[size_t(bus)]; }

        // Once per frame: bus volumes from the cvars, recycles finished voices
        void update();

        // --- sounds ---

        // A sound file by its path under assets/ ("audio/ui/click.wav"): loaded on first use, kept until
        // release_sounds. Invalid (logged once) if the file does not exist
        SoundId get_sound(std::string_view asset_path, const LoadDesc& desc = {});
        // Releases all sounds loaded by path (level change); voices playing them finish normally
        void release_sounds();
        SoundState get_sound_state(SoundId sound) const { return backend->get_sound_state(sound); }
        float get_sound_length(SoundId sound) const { return backend->get_sound_length(sound); }

        // Sound files under assets/ (wav, mp3, flac, ogg), relative paths with '/'
        std::vector<std::string> find_sound_assets() const;

        // --- voices ---

        // An invalid params.bus plays on the sfx bus
        VoiceId play(SoundId sound, PlayParams params = {});
        VoiceId play(std::string_view asset_path, PlayParams params = {});
        // Spatial with the default distances at position
        VoiceId play_at(std::string_view asset_path, const glm::vec3& position, PlayParams params = {});

        void stop(VoiceId voice, float fade = 0.0f) { backend->stop(voice, fade); }
        void stop_all() { backend->stop_all(); }
        bool is_alive(VoiceId voice) const { return backend->is_alive(voice); }
        void set_volume(VoiceId voice, float volume) { backend->set_volume(voice, volume); }
        void set_pitch(VoiceId voice, float pitch) { backend->set_pitch(voice, pitch); }
        void set_spatial(VoiceId voice, const Spatial& spatial) { backend->set_spatial(voice, spatial); }
        // Hz, 0: no filter (PlayParams::lowpass)
        void set_lowpass(VoiceId voice, float cutoff) { backend->set_lowpass(voice, cutoff); }
        float get_cursor(VoiceId voice) const { return backend->get_cursor(voice); }

        void set_listener(const Listener& in_listener);
        const Listener& get_listener() const { return listener; }

    private:
        void create_buses();
        void apply_volumes(bool force);

        std::unique_ptr<Backend> backend;
        std::array<BusId, size_t(Bus::count)> buses{};
        std::array<float, size_t(Bus::count)> applied_volumes{};
        Listener listener;

        struct SoundKey
        {
            std::string path;
            bool stream = false;
            friend bool operator==(const SoundKey&, const SoundKey&) = default;
        };
        struct SoundKeyHash
        {
            size_t operator()(const SoundKey& key) const
            {
                return std::hash<std::string>{}(key.path) ^ (key.stream ? 0x9e3779b97f4a7c15ull : 0);
            }
        };
        std::unordered_map<SoundKey, SoundId, SoundKeyHash> sounds;
    };
}
