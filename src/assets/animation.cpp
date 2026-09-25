module;

#include <json/value.h>

module assets;

import :animation;

import std.compat;
import assertions;
import fixed_string;

import glm;

import fastgltf;
import fastgltf_helper;
import log;
import :asset_manager;

#include "common/assertion_macros.h"
#include "logging/log_macro.h"

DEFINE_LOGGER(LogAnimation, Log);


glm::mat4 BoneTransform::matrix() const
{
    return glm::translate(glm::mat4(1.0f), translation) *
        glm::mat4_cast(rotation) *
        glm::scale(glm::mat4(1.0f), scale);
}


std::optional<AnimationClip> AnimationClip::create_from_file(const std::filesystem::path& path)
{
    fastgltf::Parser parser;

    constexpr auto gltf_options =
        fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::AllowDouble |
        fastgltf::Options::LoadExternalBuffers;

    auto gltf_file = fastgltf::MappedGltfFile::FromPath(path);
    if (!gltf_file)
    {
        LogAnimation.Log("Failed to open %s", path.string().c_str());
        return std::nullopt;
    }

    auto asset_result = parser.loadGltf(gltf_file.get(), path.parent_path(), gltf_options);
    if (asset_result.error() != fastgltf::Error::None)
    {
        LogAnimation.Log("Failed to load %s: error %d", path.string().c_str(), (int)asset_result.error());
        return std::nullopt;
    }

    fastgltf::Asset& asset = asset_result.get();
    if (asset.animations.empty())
    {
        LogAnimation.Log("No animations in %s", path.string().c_str());
        return std::nullopt;
    }

    const fastgltf::Animation& animation = asset.animations[0];

    AnimationClip clip;
    clip.name = animation.name.c_str();

    // one track per animated node
    std::vector<int32_t> node_to_track(asset.nodes.size(), -1);

    auto read_times = [&](size_t accessor_index)
    {
        std::vector<float> times;
        fastgltf::iterateAccessor<float>(asset, asset.accessors[accessor_index], [&](float t) { times.push_back(t); });
        return times;
    };

    for (const fastgltf::AnimationChannel& channel : animation.channels)
    {
        if (!channel.nodeIndex.has_value())
            continue;

        const size_t node_index = *channel.nodeIndex;
        const fastgltf::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];

        if (sampler.interpolation != fastgltf::AnimationInterpolation::Linear)
            LogAnimation.Log("%s: non linear sampler is played as linear", path.filename().string().c_str());

        if (node_to_track[node_index] < 0)
        {
            node_to_track[node_index] = (int32_t)clip.tracks.size();
            clip.tracks.push_back({});
            clip.tracks.back().bone_name = asset.nodes[node_index].name.c_str();
        }
        AnimationTrack& track = clip.tracks[node_to_track[node_index]];

        std::vector<float> times = read_times(sampler.inputAccessor);
        if (!times.empty())
            clip.duration = std::max(clip.duration, times.back());

        const fastgltf::Accessor& output = asset.accessors[sampler.outputAccessor];

        switch (channel.path)
        {
        case fastgltf::AnimationPath::Translation:
            track.translation_times = std::move(times);
            fastgltf::iterateAccessor<fastgltf::math::fvec3>(asset, output,
                [&](const fastgltf::math::fvec3& v) { track.translations.push_back(fastgltf_to_glm(v)); });
            break;
        case fastgltf::AnimationPath::Rotation:
            track.rotation_times = std::move(times);
            fastgltf::iterateAccessor<fastgltf::math::fvec4>(asset, output,
                [&](const fastgltf::math::fvec4& v) { track.rotations.push_back(glm::quat(v.w(), v.x(), v.y(), v.z())); });
            break;
        case fastgltf::AnimationPath::Scale:
            track.scale_times = std::move(times);
            fastgltf::iterateAccessor<fastgltf::math::fvec3>(asset, output,
                [&](const fastgltf::math::fvec3& v) { track.scales.push_back(fastgltf_to_glm(v)); });
            break;
        default:
            break;
        }
    }

    if (clip.name.empty())
        clip.name = path.stem().string();

    LogAnimation.Log("Loaded animation %s: %zu tracks, %.3fs", clip.name.c_str(), clip.tracks.size(), clip.duration);
    return clip;
}


const AnimationClip& AnimationClipHandle::get() const
{
    return AssetManager::get().get_animation(*this);
}


AnimationBinding AnimationBinding::bind(const AnimationClip& clip, const Skeleton& skeleton)
{
    AnimationBinding binding;
    binding.track_to_bone.reserve(clip.tracks.size());

    uint32_t missing = 0;
    for (const AnimationTrack& track : clip.tracks)
    {
        auto bone = skeleton.find_bone(track.bone_name);
        binding.track_to_bone.push_back(bone ? (int32_t)*bone : -1);
        if (!bone)
            missing++;
    }
    if (missing > 0)
        LogAnimation.Log("%s: %u tracks without matching bones", clip.name.c_str(), missing);
    return binding;
}


BonePose make_bind_pose(const Skeleton& skeleton)
{
    BonePose pose(skeleton.num_bones());
    for (uint32_t i = 0; i < skeleton.num_bones(); ++i)
    {
        const SkeletonBone& bone = skeleton.bones[i];
        pose[i].translation = bone.bind_translation;
        pose[i].rotation = bone.bind_rotation;
        pose[i].scale = bone.bind_scale;
    }
    return pose;
}


// finds keys around `time`: returns index of the left key and blend factor
static void find_keys(const std::vector<float>& times, float time, size_t& index, float& alpha)
{
    if (times.size() < 2 || time <= times.front())
    {
        index = 0;
        alpha = 0.0f;
        return;
    }
    if (time >= times.back())
    {
        index = times.size() - 2;
        alpha = 1.0f;
        return;
    }
    const auto it = std::upper_bound(times.begin(), times.end(), time);
    index = (size_t)std::distance(times.begin(), it) - 1;
    const float span = times[index + 1] - times[index];
    alpha = span > 0.0f ? (time - times[index]) / span : 0.0f;
}

static glm::quat nlerp(glm::quat a, glm::quat b, float t)
{
    if (glm::dot(a, b) < 0.0f)
        b = -b;
    return glm::normalize(a * (1.0f - t) + b * t);
}

void sample_animation(const AnimationClip& clip, const AnimationBinding& binding, float time, bool loop, BonePose& pose)
{
    checkf(binding.track_to_bone.size() == clip.tracks.size(), "Animation binding does not match clip");

    if (clip.duration > 0.0f)
    {
        time = loop ? std::fmod(time, clip.duration) : std::clamp(time, 0.0f, clip.duration);
        if (time < 0.0f)
            time += clip.duration;
    }

    for (size_t track_index = 0; track_index < clip.tracks.size(); ++track_index)
    {
        const int32_t bone = binding.track_to_bone[track_index];
        if (bone < 0)
            continue;

        const AnimationTrack& track = clip.tracks[track_index];
        BoneTransform& out = pose[bone];

        size_t index;
        float alpha;

        if (!track.translations.empty())
        {
            find_keys(track.translation_times, time, index, alpha);
            out.translation = track.translations.size() == 1
                ? track.translations[0]
                : glm::mix(track.translations[index], track.translations[index + 1], alpha);
        }
        if (!track.rotations.empty())
        {
            find_keys(track.rotation_times, time, index, alpha);
            out.rotation = track.rotations.size() == 1
                ? track.rotations[0]
                : nlerp(track.rotations[index], track.rotations[index + 1], alpha);
        }
        if (!track.scales.empty())
        {
            find_keys(track.scale_times, time, index, alpha);
            out.scale = track.scales.size() == 1
                ? track.scales[0]
                : glm::mix(track.scales[index], track.scales[index + 1], alpha);
        }
    }
}

void blend_poses(const BonePose& a, const BonePose& b, float weight, BonePose& out)
{
    checkf(a.size() == b.size(), "Pose size mismatch");
    out.resize(a.size());
    weight = std::clamp(weight, 0.0f, 1.0f);

    for (size_t i = 0; i < a.size(); ++i)
    {
        out[i].translation = glm::mix(a[i].translation, b[i].translation, weight);
        out[i].rotation = nlerp(a[i].rotation, b[i].rotation, weight);
        out[i].scale = glm::mix(a[i].scale, b[i].scale, weight);
    }
}

std::vector<glm::mat4> pose_to_local_matrices(const BonePose& pose)
{
    std::vector<glm::mat4> result;
    result.reserve(pose.size());
    for (const BoneTransform& bone : pose)
        result.push_back(bone.matrix());
    return result;
}

void serialize_json_value(AnimationClipHandle& target, const Json::Value& value, const SerializationContext& context)
{
    if (value.isString())
        target = AssetManager::get().load_animation(value.asString());
}
