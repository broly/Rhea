export module locomotion:types;

import std.compat;
import glm;

// States of a locomotion character (concepts of ALS-Refactored, gameplay tags there, enums here).
//
// Angles: degrees, yaw like ALS - positive turns right (clockwise seen from above). Rhea headings turn the
// other way (heading 0 faces +Z, +90 faces +X, the character's left): yaw = -heading. Use the helpers below.
export namespace loco
{
    enum class Gait : uint8_t { walking, running, sprinting };
    enum class Stance : uint8_t { standing, crouching };
    enum class RotationMode : uint8_t { velocity_direction, view_direction, aiming };
    enum class ViewMode : uint8_t { third_person, first_person };
    enum class LocomotionMode : uint8_t { none, grounded, in_air };
    enum class LocomotionAction : uint8_t { none, mantling, rolling, ragdolling, getting_up };
    enum class InAirRotationMode : uint8_t { rotate_to_velocity, keep_view_space_rotation, keep_world_rotation };
    enum class MantlingType : uint8_t { high, low, in_air };

    const char* to_string(Gait v);
    const char* to_string(Stance v);
    const char* to_string(RotationMode v);
    const char* to_string(LocomotionMode v);
    const char* to_string(LocomotionAction v);

    inline constexpr float pi = 3.14159265358979f;

    // [-180, 180)
    inline float unwind_degrees(float a)
    {
        a = std::fmod(a + 180.0f, 360.0f);
        if (a < 0.0f)
            a += 360.0f;
        return a - 180.0f;
    }

    // yaw of a horizontal direction (y ignored)
    inline float direction_to_yaw(const glm::vec3& d)
    {
        return std::atan2(-d.x, d.z) * (180.0f / pi);
    }

    inline glm::vec3 yaw_to_direction(float yaw)
    {
        const float r = yaw * (pi / 180.0f);
        return glm::vec3(-std::sin(r), 0.0f, std::cos(r));
    }

    // rotation of an entity facing `yaw` (+Z forward at 0)
    inline glm::quat yaw_to_rotation(float yaw)
    {
        return glm::angleAxis(-yaw * (pi / 180.0f), glm::vec3(0, 1, 0));
    }

    // ---- ALS math helpers (UAlsMath / UAlsRotation)

    inline float damper_exact_alpha(float dt, float half_life)
    {
        return 1.0f - std::exp(-0.69314718f / (half_life + 1e-8f) * dt);
    }

    // [175, 180] -> [-185, -180]: turns of 180 degrees go counterclockwise
    inline float remap_angle_for_counter_clockwise_rotation(float a)
    {
        return a > 175.0f ? a - 360.0f : a;
    }

    inline float damper_exact_angle(float current, float target, float dt, float half_life)
    {
        float delta = unwind_degrees(target - current);
        if (std::abs(delta) < 1e-4f)
            return target;
        delta = remap_angle_for_counter_clockwise_rotation(delta);
        return unwind_degrees(current + delta * damper_exact_alpha(dt, half_life));
    }

    inline float interpolate_angle_constant(float current, float target, float dt, float speed)
    {
        float delta = unwind_degrees(target - current);
        const float max_delta = speed * dt;
        if (speed <= 0.0f || std::abs(delta) <= max_delta)
            return target;
        delta = remap_angle_for_counter_clockwise_rotation(delta);
        return unwind_degrees(current + (delta > 0.0f ? max_delta : -max_delta));
    }

    inline float lerp_angle(float from, float to, float t)
    {
        const float delta = remap_angle_for_counter_clockwise_rotation(unwind_degrees(to - from));
        return unwind_degrees(from + delta * t);
    }

    inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
    inline float lerp_clamped(float a, float b, float t) { return a + (b - a) * clamp01(t); }
    inline float map_range_clamped(float v, float in0, float in1, float out0, float out1)
    {
        const float t = in1 != in0 ? clamp01((v - in0) / (in1 - in0)) : 0.0f;
        return out0 + (out1 - out0) * t;
    }
}
