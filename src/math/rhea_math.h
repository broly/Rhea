#pragma once
#include <glm/fwd.hpp>
#include <glm/detail/type_quat.hpp>

namespace math
{
    inline glm::quat from_euler_rotation(glm::vec3 in_euler_rotation)
    {
        return glm::quat(glm::vec3(in_euler_rotation.x, in_euler_rotation.y, in_euler_rotation.z));
    }
}
