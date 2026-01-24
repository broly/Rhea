export module rhmath:math;

import glm;

export namespace math
{
    constexpr auto up = glm::vec3(0.f, 1.f, 0.f);
    
    inline glm::quat from_euler_rotation(glm::vec3 in_euler_rotation)
    {
        return glm::quat(glm::vec3(in_euler_rotation.x, in_euler_rotation.y, in_euler_rotation.z));
    }
    
    
    glm::quat lookAtQuaternion(const glm::vec3& from, const glm::vec3& to, 
                              const glm::vec3& up = math::up)
    {
        glm::vec3 direction = glm::normalize(to - from);
    
        if (glm::length(direction) < 0.0001f) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
    
        glm::mat4 lookAtMatrix = glm::lookAt(from, to, up);
        glm::mat3 rotationMatrix = glm::mat3(lookAtMatrix);
    
        return glm::quat_cast(rotationMatrix);
    }
    
    glm::quat lookRotation(glm::vec3 forward, glm::vec3 up = math::up)
    {
        forward = glm::normalize(forward);
        glm::vec3 right = glm::normalize(glm::cross(up, forward));
        up = glm::cross(forward, right);

        glm::mat3 m(right, up, forward);
        return glm::quat_cast(m);
    }
    
    glm::quat look_at(
        const glm::vec3& origin,
        const glm::vec3& target,
        const glm::vec3& up = glm::vec3(0, -1, 0)
    )
    {
        glm::vec3 forward = glm::normalize(target - origin);


        if (glm::abs(glm::dot(forward, up)) > 0.999f)
        {

            return look_at(origin, target, glm::vec3(1, 0, 0));
        }

        glm::vec3 right = glm::normalize(glm::cross(up, forward));
        glm::vec3 realUp = glm::cross(forward, right);

        glm::mat3 rotMat(right, realUp, forward);
        return glm::quat_cast(rotMat);
    }

}
