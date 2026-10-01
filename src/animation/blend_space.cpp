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

DEFINE_LOGGER(LogBlendSpace, Log);

namespace anim
{
    namespace
    {
        std::unordered_map<std::string, std::shared_ptr<const BlendSpace>> blend_space_cache;

        glm::vec3 barycentric(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c)
        {
            const glm::vec2 v0 = b - a, v1 = c - a, v2 = p - a;
            const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
            const float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
            const float denom = d00 * d11 - d01 * d01;
            if (std::abs(denom) < 1e-12f)
                return glm::vec3(1, 0, 0);
            const float v = (d11 * d20 - d01 * d21) / denom;
            const float w = (d00 * d21 - d01 * d20) / denom;
            return glm::vec3(1.0f - v - w, v, w);
        }

        // closest point to p on segment ab, t along it
        glm::vec2 closest_on_segment(glm::vec2 p, glm::vec2 a, glm::vec2 b, float& t)
        {
            const glm::vec2 ab = b - a;
            const float len2 = glm::dot(ab, ab);
            t = len2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
            return a + ab * t;
        }

        // two triangles per grid cell of the samples' distinct x / y values (when the file has no triangles)
        void triangulate_grid(BlendSpace& space)
        {
            std::vector<float> xs, ys;
            for (const auto& s : space.samples)
            {
                xs.push_back(s.position.x);
                ys.push_back(s.position.y);
            }
            std::ranges::sort(xs);
            std::ranges::sort(ys);
            xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
            ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
            auto at = [&](float x, float y) -> int32_t {
                for (uint32_t i = 0; i < space.samples.size(); ++i)
                    if (std::abs(space.samples[i].position.x - x) < 1e-4f && std::abs(space.samples[i].position.y - y) < 1e-4f)
                        return (int32_t)i;
                return -1;
            };
            for (size_t i = 0; i + 1 < xs.size(); ++i)
                for (size_t j = 0; j + 1 < ys.size(); ++j)
                {
                    const int32_t a = at(xs[i], ys[j]), b = at(xs[i + 1], ys[j]), c = at(xs[i + 1], ys[j + 1]), d = at(xs[i], ys[j + 1]);
                    if (a < 0 || b < 0 || c < 0 || d < 0)
                        continue;
                    space.triangles.push_back({ (uint32_t)a, (uint32_t)b, (uint32_t)c });
                    space.triangles.push_back({ (uint32_t)a, (uint32_t)c, (uint32_t)d });
                }
        }
    }

    glm::vec2 BlendSpace::clamp_input(glm::vec2 input) const
    {
        for (size_t i = 0; i < axes.size() && i < 2; ++i)
            input[(int)i] = std::clamp(input[(int)i], axes[i].min, axes[i].max);
        return input;
    }

    void BlendSpace::weights(glm::vec2 input, std::vector<float>& out) const
    {
        out.assign(samples.size(), 0.0f);
        if (samples.empty())
            return;
        input = clamp_input(input);

        if (is_1d())
        {
            // neighbours along x
            int32_t below = -1, above = -1;
            for (uint32_t i = 0; i < samples.size(); ++i)
            {
                const float x = samples[i].position.x;
                if (x <= input.x && (below < 0 || x > samples[below].position.x))
                    below = (int32_t)i;
                if (x >= input.x && (above < 0 || x < samples[above].position.x))
                    above = (int32_t)i;
            }
            if (below < 0) below = above;
            if (above < 0) above = below;
            const float x0 = samples[below].position.x, x1 = samples[above].position.x;
            const float t = x1 > x0 ? (input.x - x0) / (x1 - x0) : 0.0f;
            out[below] += 1.0f - t;
            out[above] += t;
            return;
        }

        // normalized space (axes of different ranges weigh the same)
        auto norm = [&](glm::vec2 p) {
            glm::vec2 r;
            for (int i = 0; i < 2; ++i)
            {
                const float range = axes[i].max - axes[i].min;
                r[i] = range > 0.0f ? (p[i] - axes[i].min) / range : 0.0f;
            }
            return r;
        };
        const glm::vec2 p = norm(input);

        for (const auto& tri : triangles)
        {
            const glm::vec3 w = barycentric(p, norm(samples[tri[0]].position), norm(samples[tri[1]].position), norm(samples[tri[2]].position));
            if (w.x >= -1e-4f && w.y >= -1e-4f && w.z >= -1e-4f)
            {
                const glm::vec3 c = glm::max(w, glm::vec3(0.0f));
                const float sum = c.x + c.y + c.z;
                for (int k = 0; k < 3; ++k)
                    out[tri[k]] += c[k] / sum;
                return;
            }
        }

        // outside: nearest point on any triangle edge
        float best = std::numeric_limits<float>::max();
        uint32_t best_a = 0, best_b = 0;
        float best_t = 0.0f;
        for (const auto& tri : triangles)
            for (int e = 0; e < 3; ++e)
            {
                const uint32_t a = tri[e], b = tri[(e + 1) % 3];
                float t;
                const glm::vec2 q = closest_on_segment(p, norm(samples[a].position), norm(samples[b].position), t);
                const float d = glm::distance(p, q);
                if (d < best)
                {
                    best = d;
                    best_a = a;
                    best_b = b;
                    best_t = t;
                }
            }
        if (best == std::numeric_limits<float>::max())
        {
            out[0] = 1.0f;
            return;
        }
        out[best_a] += 1.0f - best_t;
        out[best_b] += best_t;
    }

    std::shared_ptr<const BlendSpace> load_blend_space(const std::string& rel_path)
    {
        if (auto it = blend_space_cache.find(rel_path); it != blend_space_cache.end())
            return it->second;

        if (!std::filesystem::exists(paths::get_assets_path() / rel_path))
        {
            LogBlendSpace.Log("Blend space %s not found", rel_path.c_str());
            return nullptr;
        }
        std::optional<Json::Value> root = json_utils::load_json_asset(rel_path);
        if (!root)
            return nullptr;

        auto space = std::make_shared<BlendSpace>();
        space->path = rel_path;
        for (const Json::Value& a : (*root)["axes"])
            space->axes.push_back({ a.get("name", "").asString(), a.get("min", 0.0).asFloat(), a.get("max", 1.0).asFloat(),
                a.get("smoothing_time", 0.0).asFloat() });
        for (const Json::Value& s : (*root)["samples"])
        {
            BlendSpace::Sample sample;
            sample.clip = AssetManager::get().load_animation(s["clip"].asString());
            sample.position = glm::vec2(s.get("x", 0.0).asFloat(), s.get("y", 0.0).asFloat());
            sample.rate_scale = s.get("rate_scale", 1.0).asFloat();
            if (!sample.clip.is_valid())
            {
                LogBlendSpace.Log("%s: sample clip %s missing", rel_path.c_str(), s["clip"].asString().c_str());
                continue;
            }
            space->samples.push_back(std::move(sample));
        }
        for (const Json::Value& t : (*root)["triangles"])
            space->triangles.push_back({ t[0].asUInt(), t[1].asUInt(), t[2].asUInt() });
        if (!space->is_1d() && space->triangles.empty())
            triangulate_grid(*space);
        space->loop = (*root).get("loop", true).asBool();

        if (!space->samples.empty())
        {
            space->additive_kind = space->samples.front().clip.get().additive;
            space->additive = space->additive_kind != AdditiveKind::none;
        }

        blend_space_cache.emplace(rel_path, space);
        return space;
    }
}
