export module rhmath:math;

import glm;

export namespace math
{
    inline glm::quat from_euler_rotation(glm::vec3 in_euler_rotation)
    {
        return glm::quat(glm::vec3(in_euler_rotation.x, in_euler_rotation.y, in_euler_rotation.z));
    }
    
    
    glm::quat lookAtQuaternion(const glm::vec3& from, const glm::vec3& to, 
                              const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f))
    {

        glm::vec3 direction = glm::normalize(to - from);
    

        if (glm::length(direction) < 0.0001f) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
    

        glm::mat4 lookAtMatrix = glm::lookAt(from, to, up);
        glm::mat3 rotationMatrix = glm::mat3(lookAtMatrix);
    
        return glm::quat_cast(rotationMatrix);
    }

}
