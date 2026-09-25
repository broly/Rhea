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


// Skeletal animation clip. Bones without a track (or without a channel) keep their
// bind pose values, which is how translation retargeting is expressed (see tools/ue_import).
export struct AnimationClip
{
    DEFAULT_NON_COPYABLE(AnimationClip)

    static std::optional<AnimationClip> create_from_file(const std::filesystem::path& path);

    std::string name;
    float duration = 0.0f;
    std::vector<AnimationTrack> tracks;
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

    static AnimationBinding bind(const AnimationClip& clip, const Skeleton& skeleton);
};


export BonePose make_bind_pose(const Skeleton& skeleton);

// Overwrites animated channels of `pose` with the clip sampled at `time` (seconds).
export void sample_animation(const AnimationClip& clip, const AnimationBinding& binding, float time, bool loop, BonePose& pose);

// out = lerp(a, b, weight) per bone (nlerp for rotations). `out` may alias `a`.
export void blend_poses(const BonePose& a, const BonePose& b, float weight, BonePose& out);

export std::vector<glm::mat4> pose_to_local_matrices(const BonePose& pose);

export void serialize_json_value(AnimationClipHandle& target, const Json::Value& value, const SerializationContext& context);
