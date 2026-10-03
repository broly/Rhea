export module audio:backend;

import std.compat;

import :types;

// The interface between the engine's audio (AudioEngine) and the library that mixes and plays the sound. A
// backend is one .cpp (audio_miniaudio.cpp, audio_null.cpp) that includes its library only in its global module
// fragment: no library type reaches a BMI, the rest of the engine sees only this interface.
//
// Adding a backend (FMOD, Wwise, a console SDK): implement Backend in a new .cpp, add its factory to the
// table of audio_engine.cpp. Called from the main thread only.
export namespace audio
{
    class Backend
    {
    public:
        virtual ~Backend() = default;

        virtual BackendInfo get_info() const = 0;
        virtual Stats get_stats() const = 0;

        // Once per frame: recycles the voices that have finished
        virtual void update() = 0;

        // --- buses ---

        // A bus mixed into parent (master_bus at the top). Lives as long as the backend
        virtual BusId create_bus(std::string_view name, BusId parent) = 0;
        // fade: s from the current volume to the new one
        virtual void set_bus_volume(BusId bus, float volume, float fade = 0.0f) = 0;

        // --- sounds ---

        // Starts loading the file (decoding happens in the background); the id is valid at once
        virtual SoundId load_sound(const std::filesystem::path& file, const LoadDesc& desc) = 0;
        // Voices playing it keep their data until they finish
        virtual void release_sound(SoundId sound) = 0;
        virtual SoundState get_sound_state(SoundId sound) const = 0;
        // s, 0 while loading or unknown
        virtual float get_sound_length(SoundId sound) const = 0;

        // --- voices ---

        // Invalid if the sound failed or no voice could be created
        virtual VoiceId play(SoundId sound, const PlayParams& params) = 0;
        // fade: s; the voice is recycled after it
        virtual void stop(VoiceId voice, float fade = 0.0f) = 0;
        virtual void stop_all() = 0;
        // false once it has finished, was stopped or the handle is stale; true while it waits for its delay
        virtual bool is_alive(VoiceId voice) const = 0;

        virtual void set_volume(VoiceId voice, float volume) = 0;
        virtual void set_pitch(VoiceId voice, float pitch) = 0;
        // Moves a spatial voice (position, velocity, distances); ignored for 2D ones
        virtual void set_spatial(VoiceId voice, const Spatial& spatial) = 0;
        // Playback position, s from the start of the sound (lip sync follows it)
        virtual float get_cursor(VoiceId voice) const = 0;

        virtual void set_listener(const Listener& listener) = 0;
    };

    // Creates a backend, nullptr with the reason in error when it cannot start (no output device, ...)
    using BackendFactory = std::unique_ptr<Backend> (*)(std::string& error);

    struct BackendEntry
    {
        std::string_view name;
        BackendFactory create;
    };

    // Backends compiled into the engine, the preferred one first. "null" plays nothing (servers, tests, no device)
    std::span<const BackendEntry> get_backends();

    std::unique_ptr<Backend> create_miniaudio_backend(std::string& error);
    std::unique_ptr<Backend> create_null_backend(std::string& error);
}
