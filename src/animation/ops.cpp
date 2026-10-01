module animation;

import std.compat;
import glm;
import assets;
import assertions;

#include "common/assertion_macros.h"

namespace anim
{
    namespace
    {
        // scratch model space poses of the mesh space operations (graphs run on one thread at a time)
        thread_local BonePose scratch_a;
        thread_local BonePose scratch_b;

        float wrap_time(const AnimationClip& clip, float time, bool loop)
        {
            if (clip.duration <= 0.0f)
                return 0.0f;
            if (!loop)
                return std::clamp(time, 0.0f, clip.duration);
            time = std::fmod(time, clip.duration);
            return time < 0.0f ? time + clip.duration : time;
        }
    }

    void set_bind(const Rig& rig, Pose& out)
    {
        out.bones = rig.bind_pose;
        std::ranges::fill(out.curves, 0.0f);
    }

    void set_additive_identity(Pose& out)
    {
        for (BoneTransform& b : out.bones)
            b = BoneTransform{ glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f) };
        std::ranges::fill(out.curves, 0.0f);
    }

    void sample_curves(const Rig& rig, const AnimationClip& clip, float time, bool loop, Pose& out)
    {
        const Rig::ClipBinding& binding = rig.binding(clip);
        time = wrap_time(clip, time, loop);
        for (const auto& [clip_curve, rig_curve] : binding.curves)
            out.curves[rig_curve] = clip.sample_curve(clip.curves[clip_curve], time);
    }

    void sample(const Rig& rig, const AnimationClip& clip, float time, bool loop, Pose& out)
    {
        if (clip.is_additive())
            set_additive_identity(out);
        else
            set_bind(rig, out);

        const Rig::ClipBinding& binding = rig.binding(clip);
        time = wrap_time(clip, time, loop);
        sample_animation(clip, binding.bones, time, false, out.bones);
        for (const auto& [clip_curve, rig_curve] : binding.curves)
            out.curves[rig_curve] = clip.sample_curve(clip.curves[clip_curve], time);
        // root motion: the root stays in place, the motion is extracted (root_transform)
        if (clip.root_motion && !clip.is_additive())
            out.bones[rig.root] = rig.bind_pose[rig.root];
    }

    BoneTransform root_transform(const Rig& rig, const AnimationClip& clip, float time)
    {
        thread_local BonePose scratch;
        scratch = rig.bind_pose;
        sample_animation(clip, rig.binding(clip).bones, wrap_time(clip, time, false), false, scratch);
        return scratch[rig.root];
    }

    void blend(const Pose& a, const Pose& b, float w, Pose& out)
    {
        const size_t n = a.bones.size();
        out.bones.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            out.bones[i].translation = glm::mix(a.bones[i].translation, b.bones[i].translation, w);
            out.bones[i].rotation = nlerp(a.bones[i].rotation, b.bones[i].rotation, w);
            out.bones[i].scale = glm::mix(a.bones[i].scale, b.bones[i].scale, w);
        }
        for (size_t i = 0; i < a.curves.size(); ++i)
            out.curves[i] = a.curves[i] + (b.curves[i] - a.curves[i]) * w;
    }

    void blend_per_bone(const Pose& a, const Pose& b, std::span<const float> bone_weights, float curve_weight, Pose& out)
    {
        const size_t n = a.bones.size();
        out.bones.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            const float w = bone_weights[i];
            out.bones[i].translation = glm::mix(a.bones[i].translation, b.bones[i].translation, w);
            out.bones[i].rotation = nlerp(a.bones[i].rotation, b.bones[i].rotation, w);
            out.bones[i].scale = glm::mix(a.bones[i].scale, b.bones[i].scale, w);
        }
        for (size_t i = 0; i < a.curves.size(); ++i)
            out.curves[i] = a.curves[i] + (b.curves[i] - a.curves[i]) * curve_weight;
    }

    void blend_weighted(std::span<const Pose* const> poses, std::span<const float> weights, Pose& out)
    {
        checkf(!poses.empty() && poses.size() == weights.size(), "blend_weighted: bad input");
        float total = 0.0f;
        for (float w : weights)
            total += w;
        const float inv_total = total > 0.0f ? 1.0f / total : 0.0f;

        const Pose& first = *poses[0];
        const size_t n = first.bones.size();
        // accumulate into out (may alias the first pose: read it before writing)
        for (size_t i = 0; i < n; ++i)
        {
            glm::vec3 t(0.0f), s(0.0f);
            glm::quat r(0, 0, 0, 0);
            const glm::quat reference = first.bones[i].rotation;
            for (size_t p = 0; p < poses.size(); ++p)
            {
                const float w = weights[p] * inv_total;
                const BoneTransform& b = poses[p]->bones[i];
                t += b.translation * w;
                s += b.scale * w;
                const glm::quat q = glm::dot(reference, b.rotation) < 0.0f ? -b.rotation : b.rotation;
                r = glm::quat(r.w + q.w * w, r.x + q.x * w, r.y + q.y * w, r.z + q.z * w);
            }
            out.bones[i].translation = t;
            out.bones[i].scale = s;
            out.bones[i].rotation = glm::length(r) > 1e-8f ? glm::normalize(r) : reference;
        }
        for (size_t c = 0; c < first.curves.size(); ++c)
        {
            float v = 0.0f;
            for (size_t p = 0; p < poses.size(); ++p)
                v += poses[p]->curves[c] * weights[p] * inv_total;
            out.curves[c] = v;
        }
    }

    void apply_additive(const Rig& rig, Pose& base, const Pose& additive, AdditiveKind kind, float weight)
    {
        if (weight <= 0.0f || kind == AdditiveKind::none)
            return;
        const size_t n = base.bones.size();

        // translations and scales are local in both kinds
        for (size_t i = 0; i < n; ++i)
        {
            base.bones[i].translation += additive.bones[i].translation * weight;
            base.bones[i].scale *= glm::mix(glm::vec3(1.0f), additive.bones[i].scale, weight);
        }

        if (kind == AdditiveKind::local)
        {
            for (size_t i = 0; i < n; ++i)
                base.bones[i].rotation = glm::normalize(scale_rotation(additive.bones[i].rotation, weight) * base.bones[i].rotation);
        }
        else
        {
            // rotation offsets in model space: rotate every bone's model rotation, then back to local
            BonePose& model = scratch_a;
            rig.to_model(base.bones, model);
            for (size_t i = 0; i < n; ++i)
                model[i].rotation = glm::normalize(scale_rotation(additive.bones[i].rotation, weight) * model[i].rotation);
            // only rotations changed: rebuild local rotations from the parents' new model rotations
            for (uint32_t bone : rig.order)
            {
                const int32_t parent = rig.parents[bone];
                base.bones[bone].rotation = parent < 0 ? model[bone].rotation
                    : glm::normalize(glm::inverse(model[parent].rotation) * model[bone].rotation);
            }
        }

        for (size_t c = 0; c < base.curves.size(); ++c)
            base.curves[c] += additive.curves[c] * weight;
    }

    void make_additive(const Rig& rig, const Pose& pose, const Pose& base, AdditiveKind kind, Pose& out)
    {
        const size_t n = pose.bones.size();
        if (kind == AdditiveKind::mesh_rotation)
        {
            rig.to_model(pose.bones, scratch_a);
            rig.to_model(base.bones, scratch_b);
        }
        for (size_t i = 0; i < n; ++i)
        {
            const BoneTransform& p = pose.bones[i];
            const BoneTransform& b = base.bones[i];
            BoneTransform d;
            d.translation = p.translation - b.translation;
            d.scale = p.scale / glm::max(b.scale, glm::vec3(1e-6f));
            d.rotation = kind == AdditiveKind::mesh_rotation
                ? glm::normalize(scratch_a[i].rotation * glm::inverse(scratch_b[i].rotation))
                : glm::normalize(p.rotation * glm::inverse(b.rotation));
            out.bones[i] = d;
        }
        for (size_t c = 0; c < pose.curves.size(); ++c)
            out.curves[c] = pose.curves[c] - base.curves[c];
    }

    void layered_blend(const Rig& rig, Pose& base, const Pose& layer, const BoneMask& mask, float alpha,
        bool mesh_space_rotation, CurveBlend curves)
    {
        const size_t n = base.bones.size();
        if (mesh_space_rotation)
        {
            BonePose& base_model = scratch_a;
            BonePose& layer_model = scratch_b;
            rig.to_model(base.bones, base_model);
            rig.to_model(layer.bones, layer_model);
            for (size_t i = 0; i < n; ++i)
            {
                const float w = mask.weights[i] * alpha;
                if (w > 0.0f)
                    base_model[i].rotation = nlerp(base_model[i].rotation, layer_model[i].rotation, w);
            }
            for (uint32_t bone : rig.order)
            {
                const float w = mask.weights[bone] * alpha;
                const int32_t parent = rig.parents[bone];
                BoneTransform& b = base.bones[bone];
                b.translation = glm::mix(b.translation, layer.bones[bone].translation, w);
                b.scale = glm::mix(b.scale, layer.bones[bone].scale, w);
                b.rotation = parent < 0 ? base_model[bone].rotation
                    : glm::normalize(glm::inverse(base_model[parent].rotation) * base_model[bone].rotation);
            }
        }
        else
        {
            for (size_t i = 0; i < n; ++i)
            {
                const float w = mask.weights[i] * alpha;
                if (w <= 0.0f)
                    continue;
                BoneTransform& b = base.bones[i];
                b.translation = glm::mix(b.translation, layer.bones[i].translation, w);
                b.rotation = nlerp(b.rotation, layer.bones[i].rotation, w);
                b.scale = glm::mix(b.scale, layer.bones[i].scale, w);
            }
        }

        for (size_t c = 0; c < base.curves.size(); ++c)
        {
            switch (curves)
            {
            case CurveBlend::blend_by_weight:
                base.curves[c] += (layer.curves[c] - base.curves[c]) * alpha;
                break;
            case CurveBlend::use_base:
                break;
            case CurveBlend::override:
                if (layer.curves[c] != 0.0f)
                    base.curves[c] = layer.curves[c];
                break;
            case CurveBlend::use_max:
                base.curves[c] = std::max(base.curves[c], layer.curves[c]);
                break;
            }
        }
    }

    void copy_bones(const Pose& from, Pose& to, std::span<const uint32_t> bones)
    {
        for (uint32_t b : bones)
            to.bones[b] = from.bones[b];
    }

    BoneTransform model_transform(const Rig& rig, const Pose& pose, uint32_t bone)
    {
        BoneTransform t = pose.bones[bone];
        for (int32_t p = rig.parents[bone]; p >= 0; p = rig.parents[p])
            t = compose(pose.bones[p], t);
        return t;
    }
}
