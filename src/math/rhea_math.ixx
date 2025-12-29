export module rhmath:math;

import glm;

export namespace math
{
    inline glm::quat from_euler_rotation(glm::vec3 in_euler_rotation)
    {
        return glm::quat(glm::vec3(in_euler_rotation.x, in_euler_rotation.y, in_euler_rotation.z));
    }
}
