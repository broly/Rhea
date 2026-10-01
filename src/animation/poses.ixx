export module animation:poses;

import std.compat;
import glm;
import assets;
import reflect;
import framework;
import :core;
import :graph;

// Two presets usable by any creature:
//  * PoseLibrary - looping full body poses picked by index (emotes, idles); a controller puts it in its graph
//    with apply_pose_library and decides when a pose ends (moving, ...)
//  * MorphExpressions - facial expressions = sets of morph target weights, crossfaded on the SkinnedMesh
export namespace anim
{
    struct [[=scene::runtime_only]] PoseLibrary
    {
        struct Entry
        {
            std::string name;
            AnimationClipHandle clip;
        };
        std::vector<Entry> poses;
        [[=rh::edit, =rh::read_only]] int32_t active = -1;     // -1: none (the controller's own pose)
        float blend_time = 0.25f;

        std::vector<SequencePlayer> players;
        BlendList list;

        // {"poses": [{"name": "...", "clip": "....gltf"}]}
        bool load(const std::string& json_path);
        void select(int32_t index);
        int32_t count() const { return (int32_t)poses.size(); }
    };

    // source crossfaded with the selected pose (a pose restarts when selected)
    PoseRef apply_pose_library(Graph& g, PoseLibrary& library, PoseRef source);

    struct [[=scene::runtime_only]] MorphExpressions
    {
        struct Expression
        {
            std::string name;
            std::vector<float> weights;   // one per morph target of the mesh
        };
        std::vector<Expression> expressions;
        [[=rh::edit, =rh::read_only]] int32_t active = -1;     // -1: neutral face
        float blend_time = 0.2f;

        std::vector<float> from;           // weights when the blend started
        std::vector<float> weights;        // current
        float blend = 1.0f;                // 0..1 from -> target

        // {"expressions": [{"name": "...", "weights": {"eyeBlinkLeft": 1.0, ...}}]}, morph targets by name
        bool load(const std::string& json_path, const SkeletalMesh& mesh);
        void select(int32_t index);
        int32_t count() const { return (int32_t)expressions.size(); }
    };
}
