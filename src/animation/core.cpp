module;

#include <json/value.h>

module animation;

import std.compat;
import glm;
import assets;
import json_utils;
import log;
import fixed_string;
import assertions;

#include "common/assertion_macros.h"
#include "logging/log_macro.h"

DEFINE_LOGGER(LogAnim, Log);

namespace anim
{
    // ---------------------------------------------------------------- smoothing

    void spring_damper(float& x, float& v, float target, float target_velocity, float dt, float frequency, float damping_ratio)
    {
        // https://theorangeduck.com/page/spring-roll-call (stiffness k = w^2, damping d = 2 zeta w)
        if (dt <= 0.0f)
            return;
        const float w = frequency * 2.0f * 3.14159265f;
        if (w < 1e-6f)
        {
            x += v * dt;
            return;
        }
        const float k = w * w;
        const float d = 2.0f * damping_ratio * w;
        const float c = target + d * target_velocity / k;
        const float y = d * 0.5f;
        if (std::abs(damping_ratio - 1.0f) < 1e-4f)
        {
            const float j0 = x - c;
            const float j1 = v + j0 * y;
            const float e = std::exp(-y * dt);
            x = j0 * e + dt * j1 * e + c;
            v = -y * j0 * e - y * dt * j1 * e + j1 * e;
        }
        else if (damping_ratio < 1.0f)
        {
            const float wd = std::sqrt(std::max(k - y * y, 1e-8f));
            const float x0 = x - c;
            float j = std::sqrt((v + y * x0) * (v + y * x0) / (wd * wd) + x0 * x0);
            const float p = std::atan((v + x0 * y) / (-x0 * wd + 1e-8f));
            j = x0 > 0.0f ? j : -j;
            const float e = std::exp(-y * dt);
            x = j * e * std::cos(wd * dt + p) + c;
            v = -y * j * e * std::cos(wd * dt + p) - wd * j * e * std::sin(wd * dt + p);
        }
        else
        {
            const float root = std::sqrt(d * d - 4.0f * k);
            const float y0 = (d + root) * 0.5f;
            const float y1 = (d - root) * 0.5f;
            const float j1 = (c * y0 - x * y0 - v) / (y1 - y0);
            const float j0 = x - j1 - c;
            const float e0 = std::exp(-y0 * dt), e1 = std::exp(-y1 * dt);
            x = j0 * e0 + j1 * e1 + c;
            v = -y0 * j0 * e0 - y1 * j1 * e1;
        }
    }


    // ---------------------------------------------------------------- curves

    uint32_t CurveSet::add(std::string_view name)
    {
        if (auto found = find(name))
            return *found;
        const uint32_t i = (uint32_t)names.size();
        names.emplace_back(name);
        index.emplace(names.back(), i);
        return i;
    }

    std::optional<uint32_t> CurveSet::find(std::string_view name) const
    {
        auto it = index.find(std::string(name));
        return it != index.end() ? std::optional(it->second) : std::nullopt;
    }


    // ---------------------------------------------------------------- poses

    PoseRef& PoseRef::operator=(PoseRef&& other) noexcept
    {
        if (this != &other)
        {
            release();
            pose = std::exchange(other.pose, nullptr);
            pool = std::exchange(other.pool, nullptr);
        }
        return *this;
    }

    void PoseRef::release()
    {
        if (pose && pool)
            pool->give_back(pose);
        pose = nullptr;
        pool = nullptr;
    }

    void PosePool::init(uint32_t in_num_bones, uint32_t in_num_curves)
    {
        checkf(in_use() == 0, "PosePool re-initialized with poses in use");
        num_bones = in_num_bones;
        num_curves = in_num_curves;
        storage.clear();
        free.clear();
    }

    PoseRef PosePool::acquire()
    {
        if (free.empty())
        {
            auto pose = std::make_unique<Pose>();
            pose->bones.resize(num_bones);
            pose->curves.resize(num_curves, 0.0f);
            free.push_back(pose.get());
            storage.push_back(std::move(pose));
        }
        Pose* pose = free.back();
        free.pop_back();
        return PoseRef(pose, this);
    }


    // ---------------------------------------------------------------- blend curves

    float CustomCurve::evaluate(float alpha) const
    {
        if (samples.empty())
            return alpha;
        if (samples.size() == 1)
            return samples[0];
        const float position = std::clamp(alpha, 0.0f, 1.0f) * float(samples.size() - 1);
        const size_t i = std::min((size_t)position, samples.size() - 2);
        return glm::mix(samples[i], samples[i + 1], position - float(i));
    }

    float BlendShape::operator()(float a) const
    {
        a = std::clamp(a, 0.0f, 1.0f);
        switch (curve)
        {
        case BlendCurve::linear: return a;
        case BlendCurve::cubic: return a * a * (3.0f - 2.0f * a);
        case BlendCurve::quadratic_in_out: return a < 0.5f ? 2.0f * a * a : 1.0f - 2.0f * (1.0f - a) * (1.0f - a);
        case BlendCurve::sinusoidal: return 0.5f - 0.5f * std::cos(a * 3.14159265f);
        case BlendCurve::custom: return custom ? custom->evaluate(a) : a;
        }
        return a;
    }


    // ---------------------------------------------------------------- blend profiles

    float BlendProfile::bone_weight(uint32_t bone, float alpha, const BlendShape& curve) const
    {
        const float scale = bone < scales.size() ? scales[bone] : 1.0f;
        switch (mode)
        {
        case Mode::time_factor:
        {
            if (scale >= 1.0f - 1e-5f)
                return curve(alpha);
            const float factor = std::clamp(scale, 0.0f, 1.0f);
            return factor > 1e-5f ? curve(std::clamp(alpha / factor, 0.0f, 1.0f)) : 1.0f;
        }
        case Mode::weight_factor:
            return std::clamp(curve(alpha) * scale, 0.0f, 1.0f);
        }
        return curve(alpha);
    }

    std::unordered_map<std::string, BlendProfile> load_blend_profiles(const std::string& rel_path, const Rig& rig)
    {
        std::unordered_map<std::string, BlendProfile> out;
        std::optional<Json::Value> root = json_utils::load_json_asset(rel_path);
        if (!root)
            return out;
        for (const std::string& name : root->getMemberNames())
        {
            const Json::Value& entry = (*root)[name];
            BlendProfile profile;
            profile.mode = entry.get("mode", "WeightFactor").asString() == "TimeFactor"
                ? BlendProfile::Mode::time_factor : BlendProfile::Mode::weight_factor;
            profile.scales.assign(rig.num_bones(), 1.0f);
            const Json::Value& bones = entry["bones"];
            for (const std::string& bone : bones.getMemberNames())
                if (auto index = rig.find_bone(bone))
                    profile.scales[*index] = bones[bone].asFloat();
            out.emplace(name, std::move(profile));
        }
        return out;
    }


    // ---------------------------------------------------------------- rig

    std::shared_ptr<Rig> Rig::create(const Skeleton& skeleton, std::span<const std::string_view> curve_names)
    {
        auto rig = std::make_shared<Rig>();
        rig->skeleton = &skeleton;
        rig->parents.reserve(skeleton.num_bones());
        for (const SkeletonBone& bone : skeleton.bones)
            rig->parents.push_back(bone.parent);
        rig->order = skeleton.evaluation_order;
        if (rig->order.size() != skeleton.num_bones())
        {
            rig->order.resize(skeleton.num_bones());
            std::iota(rig->order.begin(), rig->order.end(), 0u);
        }
        rig->bind_pose = make_bind_pose(skeleton);
        for (std::string_view name : curve_names)
            rig->curves.add(name);
        for (uint32_t bone : rig->order)
            if (rig->parents[bone] < 0)
            {
                rig->root = bone;
                break;
            }
        return rig;
    }

    bool Rig::is_descendant(uint32_t bone, uint32_t ancestor) const
    {
        for (int32_t b = (int32_t)bone; b >= 0; b = parents[b])
            if (b == (int32_t)ancestor)
                return true;
        return false;
    }

    std::optional<uint32_t> Rig::find_bone(std::string_view name) const
    {
        return skeleton->find_bone(std::string(name));
    }

    uint32_t Rig::bone(std::string_view name) const
    {
        auto index = find_bone(name);
        checkf(index.has_value(), "Bone %.*s not found in the skeleton", (int)name.size(), name.data());
        return *index;
    }

    const Rig::ClipBinding& Rig::binding(const AnimationClip& clip) const
    {
        auto it = bindings.find(&clip);
        if (it != bindings.end())
            return it->second;

        ClipBinding binding;
        binding.bones = AnimationBinding::bind(clip, *skeleton);
        for (uint32_t i = 0; i < clip.curves.size(); ++i)
            if (auto index = curves.find(clip.curves[i].name))
                binding.curves.emplace_back(i, *index);
        return bindings.emplace(&clip, std::move(binding)).first->second;
    }

    BoneMask Rig::make_mask(std::span<const BoneMask::Branch> branches) const
    {
        BoneMask mask;
        mask.weights.assign(num_bones(), 0.0f);
        std::vector<uint8_t> excluded(num_bones(), 0);

        // depth of every bone below each branch root (UE FAnimationRuntime::CreateMaskWeights)
        for (const BoneMask::Branch& branch : branches)
        {
            auto root = find_bone(branch.bone);
            if (!root)
            {
                LogAnim.Log("Bone mask: no bone %s", branch.bone.c_str());
                continue;
            }
            for (uint32_t bone : order)
            {
                // levels from the branch root (-1: not below it)
                int32_t depth = 0;
                int32_t b = (int32_t)bone;
                while (b >= 0 && b != (int32_t)*root)
                {
                    b = parents[b];
                    ++depth;
                }
                if (b < 0)
                    continue;
                if (branch.blend_depth < 0)
                {
                    excluded[bone] = 1;
                    continue;
                }
                const float w = branch.blend_depth > 0 ? std::min(float(depth + 1) / float(branch.blend_depth), 1.0f) : 1.0f;
                mask.weights[bone] = std::max(mask.weights[bone], w);
            }
        }
        for (uint32_t bone = 0; bone < num_bones(); ++bone)
            if (excluded[bone])
                mask.weights[bone] = 0.0f;
        return mask;
    }

    void Rig::to_model(const BonePose& local, BonePose& model) const
    {
        model.resize(local.size());
        for (uint32_t bone : order)
        {
            const int32_t parent = parents[bone];
            model[bone] = parent < 0 ? local[bone] : compose(model[parent], local[bone]);
        }
    }

    void Rig::to_local(const BonePose& model, BonePose& local) const
    {
        local.resize(model.size());
        for (uint32_t bone : order)
        {
            const int32_t parent = parents[bone];
            local[bone] = parent < 0 ? model[bone] : relative(model[parent], model[bone]);
        }
    }
}
