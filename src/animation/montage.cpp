module;

#include <json/value.h>

module animation;

import std.compat;
import glm;
import assets;
import json_utils;
import paths;
import log;
import fixed_string;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogMontage, Log);

namespace anim
{
    namespace
    {
        std::unordered_map<std::string, std::shared_ptr<const Montage>> montage_cache;
    }

    std::string_view Montage::Notify::field(std::string_view key) const
    {
        for (const auto& [k, v] : fields)
            if (k == key)
                return v;
        return {};
    }

    float Montage::length() const
    {
        float total = 0.0f;
        for (const Segment& s : segments)
            total += (s.end - s.start) / std::max(std::abs(s.play_rate), 1e-4f);
        return total;
    }

    const Montage::Segment* Montage::segment_at(float t, float& clip_time) const
    {
        float start = 0.0f;
        for (const Segment& s : segments)
        {
            const float len = (s.end - s.start) / std::max(std::abs(s.play_rate), 1e-4f);
            if (t <= start + len || &s == &segments.back())
            {
                clip_time = s.start + std::clamp(t - start, 0.0f, len) * s.play_rate;
                return &s;
            }
            start += len;
        }
        clip_time = 0.0f;
        return nullptr;
    }

    std::shared_ptr<const Montage> Montage::from_clip(const AnimationClipHandle& clip, std::string_view slot,
        float blend_in, float blend_out, float blend_out_trigger_time)
    {
        auto montage = std::make_shared<Montage>();
        montage->slot = slot;
        montage->blend_in = blend_in;
        montage->blend_out = blend_out;
        montage->blend_out_trigger_time = blend_out_trigger_time;
        if (clip.is_valid())
        {
            montage->path = clip.get().name;
            montage->segments.push_back({ clip, 0.0f, clip.get().duration, 1.0f });
        }
        return montage;
    }

    std::shared_ptr<const Montage> load_montage(const std::string& rel_path)
    {
        if (auto it = montage_cache.find(rel_path); it != montage_cache.end())
            return it->second;
        if (!std::filesystem::exists(paths::get_assets_path() / rel_path))
        {
            LogMontage.Log("Montage %s not found", rel_path.c_str());
            return nullptr;
        }
        std::optional<Json::Value> root = json_utils::load_json_asset(rel_path);
        if (!root)
            return nullptr;

        auto montage = std::make_shared<Montage>();
        montage->path = rel_path;
        montage->slot = (*root).get("slot", "DefaultSlot").asString();
        montage->blend_in = (*root).get("blend_in", 0.25).asFloat();
        montage->blend_out = (*root).get("blend_out", 0.25).asFloat();
        montage->blend_out_trigger_time = (*root).get("blend_out_trigger_time", -1.0).asFloat();
        montage->auto_blend_out = (*root).get("auto_blend_out", true).asBool();
        for (const Json::Value& s : (*root)["segments"])
        {
            Montage::Segment segment;
            segment.clip = AssetManager::get().load_animation(s["clip"].asString());
            segment.start = s.get("start", 0.0).asFloat();
            segment.end = s.get("end", 0.0).asFloat();
            segment.play_rate = s.get("play_rate", 1.0).asFloat();
            if (segment.clip.is_valid())
            {
                if (segment.end <= segment.start)
                    segment.end = segment.clip.get().duration;
                montage->segments.push_back(std::move(segment));
            }
        }
        for (const Json::Value& n : (*root)["notifies"])
        {
            Montage::Notify notify{ n["name"].asString(), n.get("class", "").asString(), n.get("time", 0.0).asFloat(), n.get("duration", 0.0).asFloat(), {} };
            const Json::Value& fields = n["fields"];
            for (const std::string& key : fields.getMemberNames())
                notify.fields.emplace_back(key, fields[key].asString());
            montage->notifies.push_back(std::move(notify));
        }
        montage_cache.emplace(rel_path, montage);
        return montage;
    }


    // ---------------------------------------------------------------- slots

    uint32_t Slots::play(std::shared_ptr<const Montage> montage, const MontagePlay& params)
    {
        if (!montage || montage->segments.empty())
            return 0;

        // the montage playing in the slot blends out under the new one
        const float blend_in = params.blend_in >= 0.0f ? params.blend_in : montage->blend_in;
        for (MontageInstance& m : active)
            if (m.active() && m.montage->slot == montage->slot && !m.blending_out())
                begin_blend_out(m, blend_in);

        MontageInstance instance;
        instance.id = next_id++;
        instance.montage = std::move(montage);
        instance.rate = params.rate;
        instance.time = instance.previous_time = params.start_time;
        instance.blend_in = blend_in;
        instance.blend_out = params.blend_out >= 0.0f ? params.blend_out : instance.montage->blend_out;
        instance.inertial_blend_out = params.inertial_blend_out;
        instance.phase = instance.blend_in > 0.0f ? MontageInstance::Phase::blending_in : MontageInstance::Phase::playing;
        instance.weight = instance.blend_in > 0.0f ? 0.0f : 1.0f;
        active.push_back(std::move(instance));
        return active.back().id;
    }

    void Slots::begin_blend_out(MontageInstance& m, float duration)
    {
        if (m.inertial_blend_out)
        {
            // off at once, the inertialization above the slot hides the switch
            if (m.weight > 0.0f && duration > 0.0f)
                inertialization_requests.emplace_back(m.montage->slot, duration);
            m.weight = 0.0f;
            m.phase = MontageInstance::Phase::done;
            return;
        }
        m.phase = MontageInstance::Phase::blending_out;
        m.phase_time = 0.0f;
        m.blend_out_start_weight = m.weight;
        m.blend_out_duration = duration;
        if (duration <= 0.0f)
        {
            m.weight = 0.0f;
            m.phase = MontageInstance::Phase::done;
        }
    }

    float Slots::take_inertialization(std::string_view slot)
    {
        float duration = 0.0f;
        std::erase_if(inertialization_requests, [&](const auto& r) {
            if (r.first != slot)
                return false;
            duration = std::max(duration, r.second);
            return true;
        });
        return duration;
    }

    void Slots::stop(uint32_t id, float blend_out)
    {
        if (MontageInstance* m = find(id); m && m->active() && !m->blending_out())
            begin_blend_out(*m, blend_out >= 0.0f ? blend_out : m->blend_out);
    }

    void Slots::stop_slot(std::string_view slot, float blend_out)
    {
        for (MontageInstance& m : active)
            if (m.montage->slot == slot)
                stop(m.id, blend_out);
    }

    void Slots::stop_all(float blend_out)
    {
        for (MontageInstance& m : active)
            stop(m.id, blend_out);
    }

    bool Slots::is_playing(std::string_view slot) const
    {
        for (const MontageInstance& m : active)
            if (m.active() && m.montage->slot == slot)
                return true;
        return false;
    }

    bool Slots::is_playing(uint32_t id) const
    {
        const MontageInstance* m = find(id);
        return m && m->active();
    }

    const MontageInstance* Slots::find(uint32_t id) const
    {
        for (const MontageInstance& m : active)
            if (m.id == id)
                return &m;
        return nullptr;
    }

    MontageInstance* Slots::find(uint32_t id)
    {
        for (MontageInstance& m : active)
            if (m.id == id)
                return &m;
        return nullptr;
    }

    const MontageInstance* Slots::find_playing(const Montage& montage) const
    {
        for (const MontageInstance& m : active)
            if (m.active() && !m.blending_out() && m.montage.get() == &montage)
                return &m;
        return nullptr;
    }

    void Slots::set_position(uint32_t id, float time)
    {
        if (MontageInstance* m = find(id))
        {
            m->time = std::clamp(time, 0.0f, m->montage->length());
            m->previous_time = m->time;
        }
    }

    float Slots::slot_weight(std::string_view slot) const
    {
        float w = 0.0f;
        for (const MontageInstance& m : active)
            if (m.active() && m.montage->slot == slot)
                w += m.weight;
        return std::min(w, 1.0f);
    }

    float Slots::source_weight(std::string_view slot) const
    {
        float w = 0.0f;
        for (const MontageInstance& m : active)
            if (m.active() && m.montage->slot == slot && !m.montage->segments.empty()
                && m.montage->segments.front().clip.is_valid() && !m.montage->segments.front().clip.get().is_additive())
                w += m.weight;
        return std::clamp(1.0f - w, 0.0f, 1.0f);
    }

    const Montage::Notify* Slots::active_notify_state(std::string_view type) const
    {
        for (const MontageInstance& m : active)
        {
            if (!m.active())
                continue;
            for (const Montage::Notify& n : m.montage->notifies)
                if (n.duration > 0.0f && n.type == type && m.time >= n.time && m.time < n.time + n.duration)
                    return &n;
        }
        return nullptr;
    }

    void Slots::update(float dt, std::vector<std::string>& notify_events)
    {
        for (MontageInstance& m : active)
        {
            if (!m.active())
                continue;
            const float length = m.montage->length();
            m.previous_time = m.time;
            m.time = std::clamp(m.time + dt * m.rate, 0.0f, length);

            // notifies crossed this frame
            for (const Montage::Notify& n : m.montage->notifies)
                if (n.time > m.previous_time && n.time <= m.time)
                    notify_events.push_back(n.name);
            float from_clip = 0.0f, to_clip = 0.0f;
            const Montage::Segment* from_segment = m.montage->segment_at(m.previous_time, from_clip);
            const Montage::Segment* to_segment = m.montage->segment_at(m.time, to_clip);
            if (to_segment && to_segment == from_segment && to_clip > from_clip)
                for (const AnimNotify& n : to_segment->clip.get().notifies)
                    if (n.time > from_clip && n.time <= to_clip)
                        notify_events.push_back(n.name);

            m.phase_time += dt;
            switch (m.phase)
            {
            case MontageInstance::Phase::blending_in:
                m.weight = m.blend_in > 0.0f ? std::min(m.phase_time / m.blend_in, 1.0f) : 1.0f;
                if (m.weight >= 1.0f)
                {
                    m.phase = MontageInstance::Phase::playing;
                    m.phase_time = 0.0f;
                }
                break;
            case MontageInstance::Phase::playing:
                m.weight = 1.0f;
                break;
            case MontageInstance::Phase::blending_out:
                m.weight = m.blend_out_duration > 0.0f
                    ? m.blend_out_start_weight * std::max(1.0f - m.phase_time / m.blend_out_duration, 0.0f) : 0.0f;
                if (m.weight <= 0.0f)
                    m.phase = MontageInstance::Phase::done;
                break;
            case MontageInstance::Phase::done:
                break;
            }

            // auto blend out so that it ends with the montage (UE: blend out time before the end)
            if (m.montage->auto_blend_out && !m.blending_out() && m.active())
            {
                const float trigger = m.montage->blend_out_trigger_time >= 0.0f ? m.montage->blend_out_trigger_time : m.blend_out;
                if (m.rate > 0.0f && length - m.time <= trigger * m.rate)
                    begin_blend_out(m, m.blend_out);
            }
        }
        std::erase_if(active, [](const MontageInstance& m) { return !m.active(); });
    }
}
