export module animation:ik;

import std.compat;
import glm;
import assets;
import :core;

// Inverse kinematics on model space poses (Rig::to_model / to_local around the calls).
export namespace anim
{
    // Rotates `upper` and `lower` so that `end` reaches `target` (model space), the chain bending towards
    // `pole` (a point the knee / elbow points at). Out of reach: the chain straightens towards the target.
    // `end` keeps its model space rotation; the other descendants of the chain follow it.
    // weight 0..1 blends from the input pose.
    void two_bone_ik(const Rig& rig, BonePose& model, uint32_t upper, uint32_t lower, uint32_t end,
        const glm::vec3& target, const glm::vec3& pole, float weight = 1.0f);

    // After `changed` bones were moved in `model` (from `before`), their other descendants follow:
    // they keep their transform relative to the parent.
    void propagate_changes(const Rig& rig, const BonePose& before, BonePose& model, std::span<const uint32_t> changed);

    // shortest rotation turning direction a into direction b
    glm::quat rotation_between(const glm::vec3& a, const glm::vec3& b);
}
