module;

#include <json/value.h>

export module assets:animation;

import std.compat;

import glm;
import :asset;
import :skeletal_mesh;
import dependency_collector;
import rhobject;

#include "common/type_macros.h"


// Local (parent relative) bone transform.
export struct BoneTransform
{
    glm::vec3 translation = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1, 0, 0, 0);
    glm::vec3 scale = glm::vec3(1.0f);

    glm::mat4 matrix() const;
};

export using BonePose = std::vector<BoneTransform>;


// Keyframes of one bone (linear interpolation, glTF LINEAR samplers).
export struct AnimationTrack
{
    std::string bone_name;

    std::vector<float> translation_times;
    std::vector<glm::vec3> translations;

    std::vector<float> rotation_times;
    std::vector<glm::quat> rotations;

    std::vector<float> scale_times;
    std::vector<glm::vec3> scales;
};


// How the keys of an additive clip apply to a base pose (the clip stores the delta).
export enum class AdditiveKind : uint8_t
{
    none,
    local,           // local space: rotation = delta * base, translation = base + delta
    mesh_rotation,   // rotation offset in mesh (skeleton root) space, translation as `local`
};

// Named point in a clip; clips of one sync group are kept in phase by matching markers (feet down).
export struct SyncMarker
{
    std::string name;
    float time = 0.0f;
};

// Event at a time of a clip (duration > 0: a state that lasts that long).
export struct AnimNotify
{
    std::string name;
    std::string type;      // UE class of the notify, e.g. AlsAnimNotify_FootstepEffects
    float time = 0.0f;
    float duration = 0.0f;
    std::vector<std::pair<std::string, std::string>> fields;

    std::string_view field(std::string_view key) const;
};

// Float curve, one value per key of the clip (AnimationClip::curve_times).
export struct AnimationCurve
{
    std::string name;
    std::vector<float> values;
};


// Skeletal animation clip. Bones without a track (or without a channel) keep their
// bind pose values, which is how translation retargeting is expressed (see tools/ue_import).
//
// Clips written by tools/ue_import/import_als.py carry metadata in animations[0].extras of the glTF:
// curves, sync markers, notifies, additive kind, root motion and the reference translations of the
// translated bones (retargeting to another skeleton scales them by bind / reference length).
export struct AnimationClip
{
    DEFAULT_NON_COPYABLE(AnimationClip)

    static std::optional<AnimationClip> create_from_file(const std::filesystem::path& path);

    std::string name;
    float duration = 0.0f;
    std::vector<AnimationTrack> tracks;

    // When every channel has the same, evenly spaced keys (all clips of tools/ue_import): key i is at
    // i * key_interval and sampling needs no search. 0 otherwise.
    uint32_t key_count = 0;
    float key_interval = 0.0f;

    std::vector<AnimationCurve> curves;
    std::vector<float> curve_times;

    std::vector<SyncMarker> sync_markers;    // sorted by time
    std::vector<AnimNotify> notifies;        // sorted by time

    AdditiveKind additive = AdditiveKind::none;
    bool root_motion = false;
    float rate_scale = 1.0f;

    // reference (source skeleton) translation of bones with translation keys, by bone name
    std::vector<std::pair<std::string, glm::vec3>> ref_translations;

    bool is_additive() const { return additive != AdditiveKind::none; }
    const AnimationCurve* find_curve(std::string_view curve_name) const;
    // linear interpolation of a curve at `time` (clamped)
    float sample_curve(const AnimationCurve& curve, float time) const;
};


export struct AnimationClipHandle : AssetHandle<AnimationClipHandle>
{
    const AnimationClip& get() const;
};


// Maps clip tracks onto the bones of a particular skeleton (by bone name).
export struct AnimationBinding
{
    // -1 when the skeleton has no such bone
    std::vector<int32_t> track_to_bone;

    // Retargeting of translation keys (UE "AnimationScaled"): bind length / reference length of the bone,
    // 1 when the clip has no reference translation for it. Bones too close to their parent for a ratio
    // (the root of root motion clips) use the ratio of the first scaled bone (the pelvis: body height).
    std::vector<float> translation_scale;

    static AnimationBinding bind(const AnimationClip& clip, const Skeleton& skeleton);
};


export BonePose make_bind_pose(const Skeleton& skeleton);

// Overwrites animated channels of `pose` with the clip sampled at `time` (seconds).
export void sample_animation(const AnimationClip& clip, const AnimationBinding& binding, float time, bool loop, BonePose& pose);

// out = lerp(a, b, weight) per bone (nlerp for rotations). `out` may alias `a`.
export void blend_poses(const BonePose& a, const BonePose& b, float weight, BonePose& out);

export std::vector<glm::mat4> pose_to_local_matrices(const BonePose& pose);

export void serialize_json_value(AnimationClipHandle& target, const Json::Value& value, const SerializationContext& context);
