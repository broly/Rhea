module animation;

import std.compat;
import glm;
import assets;

namespace anim
{
    glm::quat rotation_between(const glm::vec3& a, const glm::vec3& b)
    {
        const float la = glm::length(a), lb = glm::length(b);
        if (la < 1e-8f || lb < 1e-8f)
            return glm::quat(1, 0, 0, 0);
        const glm::vec3 u = a / la, v = b / lb;
        const float d = glm::dot(u, v);
        if (d < -0.999999f)
        {
            // opposite: any perpendicular axis
            glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), u);
            if (glm::length(axis) < 1e-6f)
                axis = glm::cross(glm::vec3(0, 1, 0), u);
            return glm::angleAxis(3.14159265f, glm::normalize(axis));
        }
        const glm::vec3 c = glm::cross(u, v);
        return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
    }

    void propagate_changes(const Rig& rig, const BonePose& before, BonePose& model, std::span<const uint32_t> changed)
    {
        thread_local std::vector<uint8_t> moved;
        moved.assign(model.size(), 0);
        for (uint32_t b : changed)
            moved[b] = 2;   // set explicitly
        for (uint32_t bone : rig.order)
        {
            const int32_t parent = rig.parents[bone];
            if (parent < 0 || moved[bone] == 2 || !moved[parent])
                continue;
            model[bone] = compose(model[parent], relative(before[parent], before[bone]));
            moved[bone] = 1;
        }
    }

    void two_bone_ik(const Rig& rig, BonePose& model, uint32_t upper, uint32_t lower, uint32_t end,
        const glm::vec3& target, const glm::vec3& pole, float weight)
    {
        if (weight <= 0.0f)
            return;
        thread_local BonePose before;
        before = model;

        const glm::vec3 a = model[upper].translation;
        const glm::vec3 b = model[lower].translation;
        const glm::vec3 c = model[end].translation;
        const float la = glm::length(b - a);
        const float lb = glm::length(c - b);
        if (la < 1e-6f || lb < 1e-6f)
            return;

        glm::vec3 to_target = target - a;
        float d = glm::length(to_target);
        if (d < 1e-6f)
            return;
        const glm::vec3 dir = to_target / d;
        d = std::clamp(d, std::abs(la - lb) + 1e-4f, la + lb - 1e-4f);

        // bend direction: towards the pole, perpendicular to the chain direction
        glm::vec3 bend = pole - a;
        bend -= dir * glm::dot(bend, dir);
        if (glm::length(bend) < 1e-6f)
        {
            bend = b - a;
            bend -= dir * glm::dot(bend, dir);
        }
        if (glm::length(bend) < 1e-6f)
            return;
        bend = glm::normalize(bend);

        const float cos_a = std::clamp((la * la + d * d - lb * lb) / (2.0f * la * d), -1.0f, 1.0f);
        const float sin_a = std::sqrt(std::max(1.0f - cos_a * cos_a, 0.0f));
        const glm::vec3 new_b = a + (dir * cos_a + bend * sin_a) * la;
        const glm::vec3 new_c = a + dir * d;

        const glm::quat r1 = rotation_between(b - a, new_b - a);
        const glm::quat upper_rotation = glm::normalize(r1 * model[upper].rotation);
        const glm::quat r2 = rotation_between(r1 * (c - b), new_c - new_b);
        const glm::quat lower_rotation = glm::normalize(r2 * r1 * model[lower].rotation);

        auto apply = [&](uint32_t bone, const glm::vec3& t, const glm::quat& r) {
            model[bone].translation = glm::mix(before[bone].translation, t, weight);
            model[bone].rotation = nlerp(before[bone].rotation, r, weight);
        };
        apply(upper, a, upper_rotation);
        apply(lower, new_b, lower_rotation);
        apply(end, new_c, before[end].rotation);

        const uint32_t changed[] = { upper, lower, end };
        propagate_changes(rig, before, model, changed);
    }
}
