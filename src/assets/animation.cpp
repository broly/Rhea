module;

#include <json/reader.h>
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


// Every channel keyed at the same, evenly spaced times: sampling indexes the keys directly.
static void detect_uniform_keys(AnimationClip& clip)
{
    const std::vector<float>* reference = nullptr;
    auto same_keys = [&](const std::vector<float>& times) {
        if (times.empty())
            return true;
        if (!reference)
        {
            reference = &times;
            return true;
        }
        return times.size() == reference->size() && std::equal(times.begin(), times.end(), reference->begin(),
            [](float a, float b) { return std::abs(a - b) < 1e-4f; });
    };
    for (const AnimationTrack& track : clip.tracks)
        if (!same_keys(track.translation_times) || !same_keys(track.rotation_times) || !same_keys(track.scale_times))
            return;
    if (!reference || reference->empty() || std::abs(reference->front()) > 1e-4f)
        return;

    const size_t n = reference->size();
    const float interval = n > 1 ? reference->back() / float(n - 1) : 0.0f;
    for (size_t i = 0; i < n; ++i)
        if (std::abs((*reference)[i] - interval * float(i)) > 1e-3f)
            return;

    // every channel must have all keys (a constant channel with one key breaks the shared indexing)
    for (const AnimationTrack& track : clip.tracks)
        if ((!track.translations.empty() && track.translations.size() != n) ||
            (!track.rotations.empty() && track.rotations.size() != n) ||
            (!track.scales.empty() && track.scales.size() != n))
            return;

    clip.key_count = (uint32_t)n;
    clip.key_interval = interval;
}


// animations[0].extras of clips written by tools/ue_import/import_als.py
static void read_extras(const std::filesystem::path& path, AnimationClip& clip)
{
    std::ifstream file(path);
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errors;
    if (!file.is_open() || !Json::parseFromStream(reader, file, &root, &errors))
        return;
    const Json::Value& extras = root["animations"][0]["extras"];
    if (!extras.isObject())
        return;

    if (extras.isMember("duration"))
        clip.duration = std::max(clip.duration, extras["duration"].asFloat());

    const std::string additive = extras.get("additive", "").asString();
    if (additive == "local")
        clip.additive = AdditiveKind::local;
    else if (additive == "mesh_rotation")
        clip.additive = AdditiveKind::mesh_rotation;
    else if (!additive.empty())
        LogAnimation.Log("%s: unknown additive kind '%s'", clip.name.c_str(), additive.c_str());

    clip.root_motion = extras.get("root_motion", false).asBool();
    clip.rate_scale = extras.get("rate_scale", 1.0).asFloat();

    const Json::Value& curves = extras["curves"];
    for (const std::string& curve_name : curves.getMemberNames())
    {
        AnimationCurve curve{ curve_name, {} };
        for (const Json::Value& v : curves[curve_name])
            curve.values.push_back(v.asFloat());
        clip.curves.push_back(std::move(curve));
    }
    if (!clip.curves.empty())
    {
        const size_t n = clip.curves.front().values.size();
        clip.curve_times.resize(n);
        for (size_t i = 0; i < n; ++i)
            clip.curve_times[i] = n > 1 ? clip.duration * float(i) / float(n - 1) : 0.0f;
    }

    for (const Json::Value& m : extras["sync_markers"])
        clip.sync_markers.push_back({ m["name"].asString(), m["time"].asFloat() });
    std::ranges::sort(clip.sync_markers, {}, &SyncMarker::time);

    for (const Json::Value& n : extras["notifies"])
    {
        AnimNotify notify{ n["name"].asString(), n.get("class", "").asString(), n["time"].asFloat(), n.get("duration", 0.0).asFloat(), {} };
        const Json::Value& fields = n["fields"];
        for (const std::string& key : fields.getMemberNames())
            notify.fields.emplace_back(key, fields[key].asString());
        clip.notifies.push_back(std::move(notify));
    }
    std::ranges::sort(clip.notifies, {}, &AnimNotify::time);

    const Json::Value& refs = extras["ref_translations"];
    for (const std::string& bone : refs.getMemberNames())
    {
        const Json::Value& v = refs[bone];
        clip.ref_translations.emplace_back(bone, glm::vec3(v[0].asFloat(), v[1].asFloat(), v[2].asFloat()));
    }
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

    detect_uniform_keys(clip);
    read_extras(path, clip);

    LogAnimation.Log("Loaded animation %s: %zu tracks, %.3fs%s", clip.name.c_str(), clip.tracks.size(), clip.duration,
        clip.key_count > 0 ? "" : " (non uniform keys)");
    return clip;
}


std::string_view AnimNotify::field(std::string_view key) const
{
    for (const auto& [k, v] : fields)
        if (k == key)
            return v;
    return {};
}


const AnimationCurve* AnimationClip::find_curve(std::string_view curve_name) const
{
    for (const AnimationCurve& curve : curves)
        if (curve.name == curve_name)
            return &curve;
    return nullptr;
}


float AnimationClip::sample_curve(const AnimationCurve& curve, float time) const
{
    if (curve.values.empty())
        return 0.0f;
    if (curve.values.size() == 1 || curve_times.size() != curve.values.size() || time <= curve_times.front())
        return curve.values.front();
    if (time >= curve_times.back())
        return curve.values.back();
    const auto it = std::upper_bound(curve_times.begin(), curve_times.end(), time);
    const size_t i = (size_t)std::distance(curve_times.begin(), it) - 1;
    const float span = curve_times[i + 1] - curve_times[i];
    const float alpha = span > 0.0f ? (time - curve_times[i]) / span : 0.0f;
    return curve.values[i] + (curve.values[i + 1] - curve.values[i]) * alpha;
}


const AnimationClip& AnimationClipHandle::get() const
{
    return AssetManager::get().get_animation(*this);
}


AnimationBinding AnimationBinding::bind(const AnimationClip& clip, const Skeleton& skeleton)
{
    AnimationBinding binding;
    binding.track_to_bone.reserve(clip.tracks.size());

    binding.translation_scale.reserve(clip.tracks.size());

    auto ref_translation = [&](const std::string& bone_name) -> std::optional<glm::vec3> {
        for (const auto& [name, t] : clip.ref_translations)
            if (name == bone_name)
                return t;
        return std::nullopt;
    };

    // body scale: the first bone with a usable reference length (pelvis)
    float body_scale = 1.0f;
    for (const auto& [name, t] : clip.ref_translations)
    {
        auto bone = skeleton.find_bone(name);
        const float ref_length = glm::length(t);
        if (bone && ref_length > 1e-3f)
        {
            body_scale = glm::length(skeleton.bones[*bone].bind_translation) / ref_length;
            break;
        }
    }

    uint32_t missing = 0;
    for (const AnimationTrack& track : clip.tracks)
    {
        auto bone = skeleton.find_bone(track.bone_name);
        binding.track_to_bone.push_back(bone ? (int32_t)*bone : -1);
        if (!bone)
            missing++;

        float scale = 1.0f;
        if (bone && !track.translations.empty())
            if (auto ref = ref_translation(track.bone_name))
            {
                const float ref_length = glm::length(*ref);
                scale = ref_length > 1e-3f ? glm::length(skeleton.bones[*bone].bind_translation) / ref_length : body_scale;
            }
        binding.translation_scale.push_back(scale);
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

    if (clip.key_count > 0)
    {
        // all channels share evenly spaced keys
        size_t index = 0;
        float alpha = 0.0f;
        if (clip.key_count > 1 && clip.key_interval > 0.0f)
        {
            const float position = std::clamp(time / clip.key_interval, 0.0f, float(clip.key_count - 1));
            index = std::min((size_t)position, (size_t)clip.key_count - 2);
            alpha = position - float(index);
        }
        const size_t next = clip.key_count > 1 ? index + 1 : index;

        for (size_t track_index = 0; track_index < clip.tracks.size(); ++track_index)
        {
            const int32_t bone = binding.track_to_bone[track_index];
            if (bone < 0)
                continue;
            const AnimationTrack& track = clip.tracks[track_index];
            BoneTransform& out = pose[bone];
            if (!track.translations.empty())
                out.translation = glm::mix(track.translations[index], track.translations[next], alpha) * binding.translation_scale[track_index];
            if (!track.rotations.empty())
                out.rotation = nlerp(track.rotations[index], track.rotations[next], alpha);
            if (!track.scales.empty())
                out.scale = glm::mix(track.scales[index], track.scales[next], alpha);
        }
        return;
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
            out.translation = (track.translations.size() == 1
                ? track.translations[0]
                : glm::mix(track.translations[index], track.translations[index + 1], alpha)) * binding.translation_scale[track_index];
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
