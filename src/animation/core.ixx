export module animation:core;

import std.compat;
import glm;
import assets;

#include "common/type_macros.h"

// Building blocks shared by every animation graph: the skeleton a graph animates (Rig), poses with curves,
// a pool of poses so a frame allocates nothing, per bone weights (masks, blend profiles) and the math on
// bone transforms.
export namespace anim
{
    // ---------------------------------------------------------------- transforms

    inline glm::quat nlerp(const glm::quat& a, glm::quat b, float t)
    {
        if (glm::dot(a, b) < 0.0f)
            b = -b;
        return glm::normalize(a * (1.0f - t) + b * t);
    }

    // rotation from identity towards q, by t (additive weights)
    inline glm::quat scale_rotation(const glm::quat& q, float t)
    {
        return nlerp(glm::quat(1, 0, 0, 0), q, t);
    }

    // parent * child (no shear: scale is applied per axis, like UE FTransform)
    inline BoneTransform compose(const BoneTransform& parent, const BoneTransform& child)
    {
        BoneTransform out;
        out.rotation = glm::normalize(parent.rotation * child.rotation);
        out.scale = parent.scale * child.scale;
        out.translation = parent.translation + parent.rotation * (parent.scale * child.translation);
        return out;
    }

    // inverse(parent) * model: model space transform relative to `parent`
    inline BoneTransform relative(const BoneTransform& parent, const BoneTransform& model)
    {
        BoneTransform out;
        const glm::quat inv = glm::inverse(parent.rotation);
        out.rotation = glm::normalize(inv * model.rotation);
        out.scale = model.scale / parent.scale;
        out.translation = (inv * (model.translation - parent.translation)) / parent.scale;
        return out;
    }


    // ---------------------------------------------------------------- smoothing

    // Exact spring damper (UE FMath::SpringDamper): x and its velocity v move towards `target` (moving at
    // target_velocity). frequency: undamped oscillation in Hz; damping_ratio 1: critical, < 1 overshoots.
    void spring_damper(float& x, float& v, float target, float target_velocity, float dt, float frequency, float damping_ratio);


    // ---------------------------------------------------------------- curves

    // Names of the float curves a graph uses; a pose holds one value per name. Clip curves with other
    // names are not sampled. Missing curves read 0.
    struct CurveSet
    {
        std::vector<std::string> names;
        std::unordered_map<std::string, uint32_t> index;

        uint32_t add(std::string_view name);
        std::optional<uint32_t> find(std::string_view name) const;
        uint32_t size() const { return (uint32_t)names.size(); }
    };


    // ---------------------------------------------------------------- poses

    struct Pose
    {
        BonePose bones;
        std::vector<float> curves;
    };

    class PosePool;

    // A pose borrowed from a PosePool, returned when the handle dies. Move only.
    class PoseRef
    {
    public:
        PoseRef() = default;
        PoseRef(Pose* pose, PosePool* pool) : pose(pose), pool(pool) {}
        PoseRef(PoseRef&& other) noexcept : pose(std::exchange(other.pose, nullptr)), pool(std::exchange(other.pool, nullptr)) {}
        PoseRef& operator=(PoseRef&& other) noexcept;
        PoseRef(const PoseRef&) = delete;
        PoseRef& operator=(const PoseRef&) = delete;
        ~PoseRef() { release(); }

        Pose* operator->() const { return pose; }
        Pose& operator*() const { return *pose; }
        explicit operator bool() const { return pose != nullptr; }

        BoneTransform& bone(uint32_t index) const { return pose->bones[index]; }
        float& curve(uint32_t index) const { return pose->curves[index]; }

        void release();

    private:
        Pose* pose = nullptr;
        PosePool* pool = nullptr;
    };

    // Poses of one skeleton / curve set. Grows to the largest number of poses alive at once, then
    // a frame allocates nothing.
    class PosePool
    {
    public:
        void init(uint32_t num_bones, uint32_t num_curves);
        PoseRef acquire();             // contents undefined
        void give_back(Pose* pose) { free.push_back(pose); }
        uint32_t allocated() const { return (uint32_t)storage.size(); }
        uint32_t in_use() const { return (uint32_t)(storage.size() - free.size()); }

    private:
        uint32_t num_bones = 0;
        uint32_t num_curves = 0;
        std::vector<std::unique_ptr<Pose>> storage;
        std::vector<Pose*> free;
    };


    // ---------------------------------------------------------------- blend curves

    enum class BlendCurve : uint8_t
    {
        linear,
        cubic,            // smoothstep (UE EAlphaBlendOption::Cubic)
        quadratic_in_out,
        sinusoidal,
        custom,           // a sampled curve (UE custom blend curve), see BlendShape::custom
    };

    // Samples of a curve over 0..1, evenly spaced, linear between them (UE curve assets used as blend curves)
    struct CustomCurve
    {
        std::vector<float> samples;
        float evaluate(float alpha) const;
    };

    // How a weight goes from 0 to 1: a curve, or a custom table
    struct BlendShape
    {
        BlendCurve curve = BlendCurve::linear;
        const CustomCurve* custom = nullptr;   // BlendCurve::custom

        BlendShape() = default;
        BlendShape(BlendCurve curve) : curve(curve) {}
        BlendShape(const CustomCurve* custom) : curve(BlendCurve::custom), custom(custom) {}

        float operator()(float alpha) const;   // alpha clamped to 0..1
    };


    // ---------------------------------------------------------------- per bone weights

    // Weight per bone (0..1) of a layer: bone masks of layered blends, built from branches like UE branch
    // filters: a bone and its descendants, optionally reaching full weight only `blend_depth` levels down.
    struct BoneMask
    {
        std::vector<float> weights;   // per bone of the rig

        struct Branch
        {
            std::string bone;
            // 0: full weight at the bone; n > 0: the bone n levels down reaches full weight (UE: weight =
            // (depth + 1) / n); < 0: the branch is excluded (weight 0, wins over the other branches)
            int32_t blend_depth = 0;
        };
    };

    // How fast bones follow a transition (UE blend profiles), per bone scale; 1 for bones not listed.
    struct BlendProfile
    {
        enum class Mode : uint8_t
        {
            time_factor,     // bone transition time = transition time * scale (0..1)
            weight_factor,   // bone weight = transition weight * scale (>= 1: faster)
        };
        Mode mode = Mode::weight_factor;
        std::vector<float> scales;   // per bone of the rig

        // weight of a bone of the incoming pose for the transition weight `alpha` (linear 0..1, before the
        // blend curve) and its blend curve
        float bone_weight(uint32_t bone, float alpha, const BlendShape& curve) const;
    };



    // ---------------------------------------------------------------- rig

    // What a graph animates: the skeleton of a skinned mesh (bones, parents, bind pose) and the curves the
    // graph uses. Caches the bindings of clips to the skeleton.
    struct Rig
    {
        DEFAULT_NON_COPYABLE(Rig)

        const Skeleton* skeleton = nullptr;
        std::vector<int32_t> parents;
        std::vector<uint32_t> order;          // parents before children
        BonePose bind_pose;
        CurveSet curves;
        uint32_t root = 0;                    // the root bone (root motion clips move it, a pose keeps it at bind)

        static std::shared_ptr<Rig> create(const Skeleton& skeleton, std::span<const std::string_view> curve_names = {});

        uint32_t num_bones() const { return (uint32_t)parents.size(); }
        std::optional<uint32_t> find_bone(std::string_view name) const;
        uint32_t bone(std::string_view name) const;             // asserts that it exists

        // Clip tracks / curves -> rig bones / curves
        struct ClipBinding
        {
            AnimationBinding bones;
            std::vector<std::pair<uint32_t, uint32_t>> curves;   // clip curve -> rig curve
        };
        const ClipBinding& binding(const AnimationClip& clip) const;

        BoneMask make_mask(std::span<const BoneMask::Branch> branches) const;
        BoneMask make_mask(std::initializer_list<BoneMask::Branch> branches) const { return make_mask(std::span(branches.begin(), branches.size())); }

        // local -> model (skeleton root) space and back
        void to_model(const BonePose& local, BonePose& model) const;
        void to_local(const BonePose& model, BonePose& local) const;
        // true when `bone` is `ancestor` or below it
        bool is_descendant(uint32_t bone, uint32_t ancestor) const;

    private:
        mutable std::unordered_map<const AnimationClip*, ClipBinding> bindings;
    };

    // Profiles by name from a JSON file (assets/animations/als/blend_profiles.json: {"name": {"mode", "bones"}})
    std::unordered_map<std::string, BlendProfile> load_blend_profiles(const std::string& rel_path, const Rig& rig);
}
