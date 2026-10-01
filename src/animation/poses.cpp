module;

#include <json/value.h>

module animation;

import std.compat;
import glm;
import assets;
import ecs;
import framework;
import rhcomponents;
import json_utils;
import log;
import fixed_string;

#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogPoses, Log);

namespace anim
{
    bool PoseLibrary::load(const std::string& json_path)
    {
        std::optional<Json::Value> json = json_utils::load_json_asset(json_path);
        if (!json || !(*json)["poses"].isArray())
        {
            LogPoses.Log("No poses in '%s'", json_path.c_str());
            return false;
        }
        for (const Json::Value& entry : (*json)["poses"])
        {
            AnimationClipHandle clip = AssetManager::get().load_animation(entry["clip"].asString());
            if (clip.is_valid())
                poses.push_back({ entry["name"].asString(), clip });
        }
        players.resize(poses.size());
        list.reset_on_activation = true;
        LogPoses.Log("Loaded %zu poses from %s", poses.size(), json_path.c_str());
        return !poses.empty();
    }

    void PoseLibrary::select(int32_t index)
    {
        if (index >= count() || index == active)
            return;
        active = index;
        if (index >= 0)
            LogPoses.Log("Pose %d/%zu: %s", index + 1, poses.size(), poses[index].name.c_str());
        else
            LogPoses.Log("Pose: none");
    }

    PoseRef apply_pose_library(Graph& g, PoseLibrary& library, PoseRef source)
    {
        if (library.poses.empty())
            return source;
        // child 0 is the source, evaluated by the caller: handed over when it contributes
        return g.blend_list(library.list, library.active + 1, (uint32_t)library.poses.size() + 1, library.blend_time,
            [&](uint32_t i) { return i == 0 ? std::move(source) : g.play(library.players[i - 1], library.poses[i - 1].clip); });
    }

    bool MorphExpressions::load(const std::string& json_path, const SkeletalMesh& mesh)
    {
        std::optional<Json::Value> json = json_utils::load_json_asset(json_path);
        if (!json || !(*json)["expressions"].isArray())
        {
            LogPoses.Log("No expressions in '%s'", json_path.c_str());
            return false;
        }
        if (mesh.num_morph_targets() == 0)
        {
            LogPoses.Log("Mesh '%s' has no morph targets, expressions are ignored", mesh.name.c_str());
            return false;
        }
        for (const Json::Value& entry : (*json)["expressions"])
        {
            Expression expression;
            expression.name = entry["name"].asString();
            expression.weights.assign(mesh.num_morph_targets(), 0.0f);
            const Json::Value& w = entry["weights"];
            for (const std::string& target_name : w.getMemberNames())
            {
                std::optional<uint32_t> target = mesh.find_morph_target(target_name);
                if (!target)
                {
                    LogPoses.Log("Expression '%s': unknown morph target '%s'", expression.name.c_str(), target_name.c_str());
                    continue;
                }
                expression.weights[*target] = std::clamp(w[target_name].asFloat(), 0.0f, 1.0f);
            }
            expressions.push_back(std::move(expression));
        }
        weights.assign(mesh.num_morph_targets(), 0.0f);
        from = weights;
        LogPoses.Log("Loaded %zu expressions", expressions.size());
        return true;
    }

    void MorphExpressions::select(int32_t index)
    {
        if (index >= count() || index == active)
            return;
        // blend from wherever the face is now (also mid-blend)
        from = weights;
        blend = 0.0f;
        active = index;
        LogPoses.Log("Expression: %s", index >= 0 ? expressions[index].name.c_str() : "neutral");
    }

    namespace
    {
        float move_towards(float value, float target, float max_delta)
        {
            if (std::abs(target - value) <= max_delta)
                return target;
            return value + (target > value ? max_delta : -max_delta);
        }

        [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<PostProcess>, =ecs::after<Evaluate>, =ecs::before<Apply>]]
        void blend_morph_expressions(ecs::Query<MorphExpressions, SkinnedMesh> faces, ecs::Res<ecs::FrameTime> time)
        {
            const float dt = std::min((float)time->dt, 0.1f);
            faces.each([&](MorphExpressions& e, SkinnedMesh& mesh) {
                if (e.weights.empty() || e.blend >= 1.0f)
                    return;
                e.blend = move_towards(e.blend, 1.0f, dt / e.blend_time);
                const float t = e.blend * e.blend * (3.0f - 2.0f * e.blend);   // smoothstep
                for (size_t i = 0; i < e.weights.size(); ++i)
                {
                    const float target = e.active >= 0 ? e.expressions[e.active].weights[i] : 0.0f;
                    e.weights[i] = glm::mix(e.from[i], target, t);
                }
                mesh.set_morph_weights(e.weights);
            });
        }

        ECS_REGISTER()
        SCENE_REGISTER_COMPONENTS(PoseLibrary, MorphExpressions)
    }
}
