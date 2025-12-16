#pragma once

#include <glm/fwd.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rhea_math.h"

struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation = glm::quat(1, 0, 0, 0);
    glm::vec3 scale{1.f, 1.f, 1.f};
    
    Transform(
        glm::vec3 position = {0.f, 0.f, 0.f}, 
        glm::quat rotation = glm::quat(1, 0, 0, 0), 
        glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f))
            : position(position), rotation(rotation), scale(scale)
    {}
    
    [[nodiscard]]
    static Transform make_from_euler(
        glm::vec3 in_position, 
        glm::vec3 in_rotation, 
        glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f))
    {
        return {
            in_position, 
            math::from_euler_rotation(in_rotation),
            scale
        };
    }

    [[nodiscard]] 
    glm::mat4 matrix() const {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

        return T * R * S;
    }
    
    [[nodiscard]] 
    glm::vec3 forward() const {
        return rotation * glm::vec3(0, 0, -1);
    }

    [[nodiscard]] 
    glm::vec3 right() const {
        return rotation * glm::vec3(1, 0, 0);
    }

    [[nodiscard]] 
    glm::vec3 up() const {
        return rotation * glm::vec3(0, 1, 0);
    }

    void set_position(glm::vec3 vec) {
        position = vec;
    }

    void set_rotation(glm::quat quat) {
        rotation = quat;
    }

    [[nodiscard]] 
    glm::mat4 get_view() const {
        return glm::lookAt(
            position,
            position + forward(),
            up()
        );
    }
};
