module audio;

import std.compat;
import glm;
import log;
import fixed_string;
import cvar;
import paths;

import :types;
import :backend;
import :engine;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogAudio, Log);

namespace
{
    using namespace audio;

    constexpr BackendEntry backend_table[] = {
        { "miniaudio", &create_miniaudio_backend },
        { "null", &create_null_backend },
    };

    cvar::Var<std::string> cv_backend("audio.backend", "miniaudio",
        "Audio backend: miniaudio, null (silence). Read at start");
    cvar::Var<bool> cv_mute("audio.mute", false, "Silence everything");

    cvar::Var<float> cv_volume_master("audio.volume.master", 1.0f, "Master volume", {.has_range = true, .min = 0.0f, .max = 1.0f});
    cvar::Var<float> cv_volume_music("audio.volume.music", 0.8f, "Music volume", {.has_range = true, .min = 0.0f, .max = 1.0f});
    cvar::Var<float> cv_volume_sfx("audio.volume.sfx", 1.0f, "Effects volume: weapons, creatures, footsteps, ambience",
        {.has_range = true, .min = 0.0f, .max = 1.0f});
    cvar::Var<float> cv_volume_voice("audio.volume.voice", 1.0f, "Dialogue volume", {.has_range = true, .min = 0.0f, .max = 1.0f});
    cvar::Var<float> cv_volume_ui("audio.volume.ui", 1.0f, "Interface sounds volume", {.has_range = true, .min = 0.0f, .max = 1.0f});

    cvar::Var<float>* volume_cvar(Bus bus)
    {
        switch (bus)
        {
        case Bus::master: return &cv_volume_master;
        case Bus::music: return &cv_volume_music;
        case Bus::sfx: return &cv_volume_sfx;
        case Bus::voice: return &cv_volume_voice;
        case Bus::ui: return &cv_volume_ui;
        default: return nullptr;
        }
    }

    // bus volume changes are short fades: no clicks when a slider moves
    constexpr float volume_fade = 0.05f;

    bool is_sound_file(const std::filesystem::path& file)
    {
        std::string extension = file.extension().string();
        std::ranges::transform(extension, extension.begin(), [] (char c) { return char(std::tolower(uint8_t(c))); });
        return extension == ".wav" || extension == ".mp3" || extension == ".flac" || extension == ".ogg";
    }
}

namespace audio
{
    std::span<const BackendEntry> get_backends()
    {
        return backend_table;
    }

    std::string_view get_bus_name(Bus bus)
    {
        switch (bus)
        {
        case Bus::master: return "master";
        case Bus::music: return "music";
        case Bus::sfx: return "sfx";
        case Bus::voice: return "voice";
        case Bus::ui: return "ui";
        default: return "?";
        }
    }

    AudioEngine::AudioEngine() = default;

    AudioEngine::~AudioEngine()
    {
        shutdown();
    }

    void AudioEngine::init(std::string_view backend_name)
    {
        shutdown();

        const std::string wanted = backend_name.empty() ? cv_backend.get() : std::string(backend_name);

        // the wanted one first, then the others in order of preference
        std::vector<const BackendEntry*> order;
        for (const BackendEntry& entry : backend_table)
            if (entry.name == wanted)
                order.push_back(&entry);
        if (order.empty())
            LogAudio.Log<Warning>("Unknown audio backend '%s'", wanted.c_str());
        for (const BackendEntry& entry : backend_table)
            if (entry.name != wanted)
                order.push_back(&entry);

        for (const BackendEntry* entry : order)
        {
            std::string error;
            backend = entry->create(error);
            if (backend)
                break;
            LogAudio.Log<Warning>("Audio backend %.*s failed: %s", int(entry->name.size()), entry->name.data(), error.c_str());
        }

        const BackendInfo info = backend->get_info();
        LogAudio.Log("Audio: %s, %s, %u Hz, %u channels", info.backend.c_str(), info.device.c_str(),
            info.sample_rate, info.channels);

        create_buses();
        apply_volumes(true);
        backend->set_listener(listener);
    }

    void AudioEngine::shutdown()
    {
        if (!backend)
            return;
        backend->stop_all();
        sounds.clear();
        backend.reset();
    }

    void AudioEngine::create_buses()
    {
        buses[size_t(Bus::master)] = master_bus;
        for (size_t i = 0; i < size_t(Bus::count); ++i)
            if (Bus(i) != Bus::master)
                buses[i] = backend->create_bus(get_bus_name(Bus(i)), master_bus);
    }

    BackendInfo AudioEngine::get_info() const
    {
        return backend ? backend->get_info() : BackendInfo{};
    }

    Stats AudioEngine::get_stats() const
    {
        return backend ? backend->get_stats() : Stats{};
    }

    void AudioEngine::apply_volumes(bool force)
    {
        for (size_t i = 0; i < size_t(Bus::count); ++i)
        {
            float volume = volume_cvar(Bus(i))->get();
            if (Bus(i) == Bus::master && cv_mute.get())
                volume = 0.0f;
            volume = std::clamp(volume, 0.0f, 1.0f);
            if (!force && volume == applied_volumes[i])
                continue;
            backend->set_bus_volume(buses[i], volume, force ? 0.0f : volume_fade);
            applied_volumes[i] = volume;
        }
    }

    void AudioEngine::update()
    {
        if (!backend)
            return;
        apply_volumes(false);
        backend->update();
    }

    SoundId AudioEngine::get_sound(std::string_view asset_path, const LoadDesc& desc)
    {
        if (!backend)
            return {};

        SoundKey key{ std::string(asset_path), desc.stream };
        if (auto it = sounds.find(key); it != sounds.end())
            return it->second;

        SoundId sound;
        const std::filesystem::path file = paths::get_assets_path() / std::filesystem::path(key.path);
        if (std::filesystem::is_regular_file(file))
            sound = backend->load_sound(file, desc);
        else
            LogAudio.Log<Warning>("Sound not found: %s", file.string().c_str());

        // also a missing one: the warning is logged once
        sounds.emplace(std::move(key), sound);
        return sound;
    }

    void AudioEngine::release_sounds()
    {
        for (const auto& [key, sound] : sounds)
            if (sound.is_valid())
                backend->release_sound(sound);
        sounds.clear();
    }

    std::vector<std::string> AudioEngine::find_sound_assets() const
    {
        std::vector<std::string> result;
        const std::filesystem::path root = paths::get_assets_path();
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(root, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        {
            if (it->is_regular_file() && is_sound_file(it->path()))
                result.push_back(std::filesystem::relative(it->path(), root).generic_string());
        }
        std::ranges::sort(result);
        return result;
    }

    VoiceId AudioEngine::play(SoundId sound, PlayParams params)
    {
        if (!backend || !sound.is_valid())
            return {};
        if (!params.bus.is_valid())
            params.bus = get_bus(Bus::sfx);
        return backend->play(sound, params);
    }

    VoiceId AudioEngine::play(std::string_view asset_path, PlayParams params)
    {
        return play(get_sound(asset_path), params);
    }

    VoiceId AudioEngine::play_at(std::string_view asset_path, const glm::vec3& position, PlayParams params)
    {
        if (!params.spatial)
            params.spatial = Spatial{};
        params.spatial->position = position;
        return play(get_sound(asset_path), params);
    }

    void AudioEngine::set_listener(const Listener& in_listener)
    {
        listener = in_listener;
        if (backend)
            backend->set_listener(listener);
    }
}
