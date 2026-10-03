export module audio:types;

import std.compat;
import glm;

export namespace audio
{
    // A loaded sound (decoded in memory or streamed from its file)
    struct SoundId
    {
        static constexpr uint32_t invalid_value = 0xffffffffu;

        uint32_t value = invalid_value;

        constexpr bool is_valid() const { return value != invalid_value; }
        friend constexpr bool operator==(SoundId a, SoundId b) = default;
    };

    // A playing instance of a sound. Generational: a handle of a voice that has finished (and whose slot was
    // reused) is just not playing any more
    struct VoiceId
    {
        static constexpr uint32_t invalid_index = 0xffffffffu;

        uint32_t index = invalid_index;
        uint32_t generation = 0;

        constexpr bool is_valid() const { return index != invalid_index; }
        friend constexpr bool operator==(VoiceId a, VoiceId b) = default;
    };

    // A mixing bus: voices and child buses are summed into it, its volume applies to all of them.
    // The master bus (BusId{0}) always exists, every other bus has a parent.
    struct BusId
    {
        static constexpr uint32_t invalid_value = 0xffffffffu;

        uint32_t value = invalid_value;

        constexpr bool is_valid() const { return value != invalid_value; }
        friend constexpr bool operator==(BusId a, BusId b) = default;
    };

    inline constexpr BusId master_bus{ 0 };

    enum class SoundState : uint8_t
    {
        loading,        // decoding in the background; a voice started now begins once the data is there
        ready,
        failed,         // the file is missing or its format is not supported
    };

    struct LoadDesc
    {
        // false: decoded once into memory, shared by all voices (short effects)
        // true: every voice decodes the file while it plays (music, ambience, long lines)
        bool stream = false;
    };

    // How the volume falls off with the distance between min_distance and max_distance
    enum class Attenuation : uint8_t
    {
        none,
        inverse,        // physically plausible: min_distance / (min_distance + rolloff * (distance - min_distance))
        linear,
        exponential,
    };

    // World space playback: panned and attenuated relative to the listener
    struct Spatial
    {
        glm::vec3 position{ 0.0f };
        glm::vec3 velocity{ 0.0f };                 // m/s, for the doppler shift
        float min_distance = 1.0f;                  // m, full volume closer than this
        float max_distance = 200.0f;                // m, no further attenuation beyond
        float rolloff = 1.0f;
        Attenuation attenuation = Attenuation::inverse;
        float doppler = 0.0f;                       // 0: off, 1: physical
    };

    struct PlayParams
    {
        BusId bus;                                  // invalid: the master bus
        float volume = 1.0f;                        // linear
        float pitch = 1.0f;                         // playback rate, 2: an octave up
        bool looping = false;
        std::optional<Spatial> spatial;             // none: 2D (music, UI)
        float fade_in = 0.0f;                       // s
        float delay = 0.0f;                         // s from now (the sound of a far explosion)
    };

    // The ears: the active camera, usually
    struct Listener
    {
        glm::vec3 position{ 0.0f };
        glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
        glm::vec3 up{ 0.0f, 1.0f, 0.0f };
        glm::vec3 velocity{ 0.0f };
    };

    struct BackendInfo
    {
        std::string backend;                        // "miniaudio", "null"
        std::string device;                         // output device name
        uint32_t sample_rate = 0;
        uint32_t channels = 0;
    };

    struct Stats
    {
        uint32_t sounds = 0;                        // loaded
        uint32_t sounds_loading = 0;
        uint32_t voices = 0;                        // alive: playing, waiting for their delay or fading out
        uint32_t buses = 0;
    };
}
