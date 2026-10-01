export module animation:ops;

import std.compat;
import glm;
import assets;
import :core;

// Stateless operations on poses. Graph nodes (animation:graph) are built from these; they are usable
// directly too. Every `out` may alias an input unless stated otherwise.
export namespace anim
{
    // Curves of a layered blend (UE ECurveBlendOption, the useful ones)
    enum class CurveBlend : uint8_t
    {
        blend_by_weight,   // lerp like the bones (weight = layer alpha)
        use_base,          // keep the base curves
        override,          // layer curves replace the base ones where the layer has a value != 0
        use_max,           // max of both
    };

    // bind pose, curves 0
    void set_bind(const Rig& rig, Pose& out);
    // identity delta, curves 0 (the neutral additive pose)
    void set_additive_identity(Pose& out);

    // `time` wraps when looping, else clamps. Additive clips start from the identity delta,
    // the others from the bind pose (bones without tracks keep it). Root motion clips keep the root at bind
    // (UE root lock): their motion is read with root_transform and moves the entity instead.
    void sample(const Rig& rig, const AnimationClip& clip, float time, bool loop, Pose& out);

    // the root bone of a clip at `time` (clamped), translation retargeted to the rig like the pose
    BoneTransform root_transform(const Rig& rig, const AnimationClip& clip, float time);

    // only the curves of the clip at `time` (no bones)
    void sample_curves(const Rig& rig, const AnimationClip& clip, float time, bool loop, Pose& out);

    // out = lerp(a, b, w)
    void blend(const Pose& a, const Pose& b, float w, Pose& out);
    // bone i: lerp(a, b, bone_weights[i]); curves by curve_weight
    void blend_per_bone(const Pose& a, const Pose& b, std::span<const float> bone_weights, float curve_weight, Pose& out);
    // sum of weighted poses, weights normalized (rotations aligned to the first pose)
    void blend_weighted(std::span<const Pose* const> poses, std::span<const float> weights, Pose& out);

    // base += additive * weight (kind: how the additive pose was made)
    void apply_additive(const Rig& rig, Pose& base, const Pose& additive, AdditiveKind kind, float weight);
    // the delta that turns `base` into `pose` (UE MakeDynamicAdditive)
    void make_additive(const Rig& rig, const Pose& pose, const Pose& base, AdditiveKind kind, Pose& out);

    // base = lerp(base, layer, mask * alpha) per bone. mesh_space_rotation: rotations blended in model space,
    // so a layer on the spine keeps its world orientation whatever the hips do (UE bMeshSpaceRotationBlend).
    void layered_blend(const Rig& rig, Pose& base, const Pose& layer, const BoneMask& mask, float alpha,
        bool mesh_space_rotation, CurveBlend curves = CurveBlend::blend_by_weight);

    // copies the transform of the given bones (and their curves: none) from `from` into `to`
    void copy_bones(const Pose& from, Pose& to, std::span<const uint32_t> bones);

    // model (skeleton root) space transform of one bone
    BoneTransform model_transform(const Rig& rig, const Pose& pose, uint32_t bone);
}
