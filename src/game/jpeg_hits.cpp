module;

#include <json/reader.h>
#include <json/value.h>

module game;

import :jpeg_hits;

import std.compat;

import glm;
import cvar;
import paths;
import :jpeg;

import log;
#include "logging/log_macro.h"

DEFINE_LOGGER(LogJpegHits, Display);

namespace
{
    struct Hit
    {
        glm::vec3 center;
        float radius;
        float strength;     // at birth
        float lifetime;
        float age = 0.0f;
        float seed;
        std::string preset;
        // into g_presets, resolved again after a reload
        size_t preset_index = 0;

        float current_strength() const
        {
            return strength * std::clamp(1.0f - age / lifetime, 0.0f, 1.0f);
        }
    };

    std::vector<Hit> g_hits;
    uint32_t g_hit_counter = 0;
    std::optional<jpeg_hits::ShootRequest> g_shoot_request;
    jpeg_hits::Frame g_frame;
    // preset index of each slot last frame (keeps a preset in its slot while it lives)
    std::array<std::optional<size_t>, jpeg_hits::slot_count> g_slot_presets;

    std::vector<JpegHitPreset> g_presets;
    bool g_presets_loaded = false;
    std::filesystem::file_time_type g_presets_time{};
    float g_reload_check = 0.0f;

    constexpr std::string_view default_preset = "default";

    std::optional<JpegChroma> parse_chroma(std::string_view text)
    {
        if (text == "4:4:4") return JpegChroma::Yuv444;
        if (text == "4:2:2") return JpegChroma::Yuv422;
        if (text == "4:2:0") return JpegChroma::Yuv420;
        if (text == "4:1:1") return JpegChroma::Yuv411;
        return std::nullopt;
    }

    std::optional<JpegFilter> parse_filter(std::string_view text)
    {
        if (text == "nearest") return JpegFilter::Nearest;
        if (text == "box") return JpegFilter::Box;
        return std::nullopt;
    }

    JpegHitPreset parse_preset(const std::string& name, const Json::Value& value)
    {
        JpegHitPreset preset;
        preset.name = name;
        JpegSettings& s = preset.settings;
        for (const std::string& key : value.getMemberNames())
        {
            const Json::Value& v = value[key];
            bool ok = true;
            if (key == "description" && v.isString()) preset.description = v.asString();
            else if (key == "downscale" && v.isNumeric()) s.downscale = v.asInt();
            else if (key == "quality" && v.isNumeric()) s.quality = v.asInt();
            else if (key == "quality_weak" && v.isNumeric()) s.mask_quality = v.asInt();
            else if (key == "generations" && v.isNumeric()) s.generations = v.asInt();
            else if (key == "sharpen" && v.isNumeric()) s.sharpen = v.asFloat();
            else if (key == "radius" && v.isNumeric()) preset.radius = std::max(v.asFloat(), 0.01f);
            else if (key == "lifetime" && v.isNumeric()) preset.lifetime = std::max(v.asFloat(), 0.01f);
            else if (key == "edge" && v.isNumeric()) preset.edge = std::clamp(v.asFloat(), 0.0f, 1.0f);
            else if (key == "chroma" && v.isString() && parse_chroma(v.asString())) s.chroma = *parse_chroma(v.asString());
            else if (key == "filter" && v.isString() && parse_filter(v.asString())) s.filter = *parse_filter(v.asString());
            else ok = false;
            if (!ok)
                LogJpegHits.Log("Preset %s: unknown field or bad value '%s'", name.c_str(), key.c_str());
        }
        s = jpeg::clamped(s);
        return preset;
    }

    size_t resolve_preset(std::string_view name)
    {
        const auto& presets = jpeg_hits::get_presets();
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == name)
                return i;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == default_preset)
                return i;
        return 0;
    }

    // how different two presets look (for the hits of a preset without a slot)
    float preset_distance(const JpegHitPreset& a, const JpegHitPreset& b)
    {
        const JpegSettings& x = a.settings;
        const JpegSettings& y = b.settings;
        return std::abs(std::log2((float)x.downscale) - std::log2((float)y.downscale)) * 4.0f
            + (x.filter != y.filter ? 2.0f : 0.0f)
            + std::abs((float)(x.quality - y.quality)) / 25.0f
            + (x.chroma != y.chroma ? 0.5f : 0.0f)
            + std::abs((float)(x.generations - y.generations)) * 0.25f;
    }

    float parse_float(cvar::Args args, size_t index, float fallback)
    {
        float value = fallback;
        if (index < args.size())
            std::from_chars(args[index].data(), args[index].data() + args[index].size(), value);
        return value;
    }

    std::vector<std::string> preset_names()
    {
        std::vector<std::string> names;
        for (const JpegHitPreset& preset : jpeg_hits::get_presets())
            names.push_back(preset.name);
        return names;
    }

    cvar::Command cmd_shoot("render.jpeg.hits.shoot", "Hits the surface in the middle of the screen (JPEG spot of a preset)",
        [] (cvar::Args args)
        {
            g_shoot_request = jpeg_hits::ShootRequest{
                .preset = args.empty() ? std::string(default_preset) : args[0],
                .strength = parse_float(args, 1, 1.0f),
                .radius = parse_float(args, 2, 0.0f),
                .lifetime = parse_float(args, 3, 0.0f),
            };
        },
        "[preset] [strength 0..1] [radius m] [lifetime s]",
        [] (size_t arg_index) { return arg_index == 0 ? preset_names() : std::vector<std::string>{}; });

    cvar::Command cmd_clear("render.jpeg.hits.clear", "Removes every JPEG spot",
        [] (cvar::Args) { jpeg_hits::clear(); });

    cvar::Command cmd_presets("render.jpeg.hits.presets", "Prints the JPEG hit presets (assets/jpeg/presets.json)",
        [] (cvar::Args)
        {
            for (const JpegHitPreset& p : jpeg_hits::get_presets())
            {
                const JpegSettings& s = p.settings;
                cvar::print(std::format("{}: downscale {} {}, quality {}..{}, chroma {}, generations {}, sharpen {}, "
                    "radius {} m, lifetime {} s, edge {}  {}", p.name, s.downscale,
                    s.filter == JpegFilter::Nearest ? "nearest" : "box", s.quality, s.mask_quality, (int)s.chroma,
                    s.generations, s.sharpen, p.radius, p.lifetime, p.edge, p.description));
            }
        });

    cvar::Command cmd_reload("render.jpeg.hits.reload", "Reloads assets/jpeg/presets.json",
        [] (cvar::Args) { jpeg_hits::reload_presets(); });
}

/************************************************************************
 * PRESETS
 ***********************************************************************/

std::filesystem::path jpeg_hits::get_presets_path()
{
    return paths::get_assets_path() / "jpeg" / "presets.json";
}

bool jpeg_hits::reload_presets()
{
    g_presets_loaded = true;
    const std::filesystem::path path = get_presets_path();
    std::error_code error;
    g_presets_time = std::filesystem::last_write_time(path, error);

    std::vector<JpegHitPreset> presets;
    std::ifstream file(path);
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!file || !Json::parseFromStream(builder, file, &root, &errors) || !root.isObject())
    {
        LogJpegHits.Log("Could not read %s: %s (the presets stay as they were)", path.string().c_str(), errors.c_str());
        if (g_presets.empty())
            g_presets.push_back({ .name = std::string(default_preset) });
        return false;
    }

    const Json::Value& hits = root["hits"];
    if (hits.isObject())
        for (const std::string& name : hits.getMemberNames())
            presets.push_back(parse_preset(name, hits[name]));
    if (std::ranges::none_of(presets, [] (const JpegHitPreset& p) { return p.name == default_preset; }))
        presets.push_back({ .name = std::string(default_preset) });

    g_presets = std::move(presets);
    for (Hit& hit : g_hits)
        hit.preset_index = resolve_preset(hit.preset);
    g_slot_presets = {};
    LogJpegHits.Log("Loaded %zu JPEG hit presets from %s", g_presets.size(), path.string().c_str());
    return true;
}

const std::vector<JpegHitPreset>& jpeg_hits::get_presets()
{
    if (!g_presets_loaded)
        reload_presets();
    return g_presets;
}

const JpegHitPreset* jpeg_hits::find_preset(std::string_view name)
{
    for (const JpegHitPreset& preset : get_presets())
        if (preset.name == name)
            return &preset;
    return nullptr;
}

/************************************************************************
 * HITS
 ***********************************************************************/

void jpeg_hits::add(const glm::vec3& center, std::string_view preset_name, float strength, float radius, float lifetime)
{
    const size_t index = resolve_preset(preset_name);
    const JpegHitPreset& preset = get_presets()[index];
    if (preset.name != preset_name)
        LogJpegHits.Log("Unknown JPEG hit preset '%.*s', using '%s'", (int)preset_name.size(), preset_name.data(),
            preset.name.c_str());

    Hit hit{
        .center = center,
        .radius = radius > 0.0f ? radius : preset.radius,
        .strength = std::clamp(strength, 0.0f, 1.0f),
        .lifetime = std::max(lifetime > 0.0f ? lifetime : preset.lifetime, 0.01f),
        // golden ratio steps: neighbouring hits get unrelated ragged borders
        .seed = std::fmod((float)++g_hit_counter * 0.618034f, 1.0f) * 1000.0f,
        .preset = preset.name,
        .preset_index = index,
    };
    if (g_hits.size() < max_hits)
    {
        g_hits.push_back(std::move(hit));
        return;
    }
    auto weakest = std::ranges::min_element(g_hits, {}, &Hit::current_strength);
    *weakest = std::move(hit);
}

void jpeg_hits::clear()
{
    g_hits.clear();
}

void jpeg_hits::tick(float delta_seconds)
{
    const float dt = std::clamp(delta_seconds, 0.0f, 0.25f);

    // hot reload: the file time twice a second
    g_reload_check += dt;
    if (g_reload_check >= 0.5f)
    {
        g_reload_check = 0.0f;
        std::error_code error;
        const auto time = std::filesystem::last_write_time(get_presets_path(), error);
        if (!error && g_presets_loaded && time != g_presets_time)
            reload_presets();
    }

    for (Hit& hit : g_hits)
        hit.age += dt;
    std::erase_if(g_hits, [] (const Hit& hit) { return hit.age >= hit.lifetime; });
}

const jpeg_hits::Frame& jpeg_hits::update()
{
    const auto& presets = get_presets();
    g_frame = {};
    if (!cv_jpeg_hits_enabled.get() || g_hits.empty())
    {
        g_slot_presets = {};
        return g_frame;
    }

    // the strongest hit of every preset in use
    std::map<size_t, float> weights;
    for (const Hit& hit : g_hits)
    {
        float& weight = weights[hit.preset_index];
        weight = std::max(weight, hit.current_strength());
    }
    std::vector<std::pair<size_t, float>> ranked(weights.begin(), weights.end());
    std::ranges::sort(ranked, std::greater{}, &std::pair<size_t, float>::second);
    if (ranked.size() > slot_count)
        ranked.resize(slot_count);

    // presets keep the slot they had, the new ones take the free slots
    std::array<std::optional<size_t>, slot_count> slots{};
    for (const auto& [preset, weight] : ranked)
        for (uint32_t s = 0; s < slot_count; ++s)
            if (g_slot_presets[s] == preset)
                slots[s] = preset;
    for (const auto& [preset, weight] : ranked)
    {
        if (std::ranges::find(slots, std::optional<size_t>(preset)) != slots.end())
            continue;
        for (uint32_t s = 0; s < slot_count; ++s)
            if (!slots[s])
            {
                slots[s] = preset;
                break;
            }
    }
    g_slot_presets = slots;

    auto slot_of = [&] (size_t preset) -> uint32_t
    {
        uint32_t best = 0;
        float best_distance = std::numeric_limits<float>::max();
        for (uint32_t s = 0; s < slot_count; ++s)
        {
            if (!slots[s])
                continue;
            if (*slots[s] == preset)
                return s;
            const float d = preset_distance(presets[*slots[s]], presets[preset]);
            if (d < best_distance)
            {
                best_distance = d;
                best = s;
            }
        }
        return best;
    };

    uint32_t count = 0;
    for (const Hit& hit : g_hits)
    {
        const float strength = hit.current_strength();
        if (strength <= 0.0f)
            continue;
        const JpegHitPreset& preset = presets[hit.preset_index];
        g_frame.ubo.spheres[count] = glm::vec4(hit.center, hit.radius);
        g_frame.ubo.params[count] = glm::vec4(strength, hit.seed, (float)slot_of(hit.preset_index), preset.edge);
        ++count;
    }
    g_frame.ubo.info = glm::uvec4(count, 0u, 0u, 0u);
    g_frame.any = count > 0;

    for (uint32_t s = 0; s < slot_count; ++s)
    {
        g_frame.slot_active[s] = g_frame.any && slots[s].has_value();
        if (!slots[s])
            continue;
        JpegSettings settings = presets[*slots[s]].settings;
        // a new hit moves the grids: the blocks jump
        settings.seed = g_hit_counter * slot_count + s;
        g_frame.slot_settings[s] = settings;
    }
    return g_frame;
}

std::optional<jpeg_hits::ShootRequest> jpeg_hits::take_shoot_request()
{
    return std::exchange(g_shoot_request, std::nullopt);
}
