export module rhmath:transform;

import :math;

import <string>;
import <sstream>;
import glm;
import :vec;
#include "common/reflect_macros.h";



export struct Transform {
    vec3 position{0.0f, 0.0f, 0.0f};
    quat rotation = glm::quat(1, 0, 0, 0);
    vec3 scale{1.f, 1.f, 1.f};
    
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
        glm::mat4 T = glm::translate(glm::mat4(1.0f), position.glm());
        glm::mat4 R = glm::mat4_cast(rotation.glm());
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale.glm());

        return T * R * S;
    }
    
    [[nodiscard]] 
    glm::vec3 forward() const {
        return rotation.glm() * glm::vec3(0, 0, -1);
    }

    [[nodiscard]] 
    glm::vec3 right() const {
        return rotation.glm() * glm::vec3(1, 0, 0);
    }

    [[nodiscard]] 
    glm::vec3 up() const {
        return rotation.glm() * glm::vec3(0, 1, 0);
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
            position.glm(),
            position.glm() + forward(),
            up()
        );
    }

    static Transform make_identity()
    {
        return Transform();
    }
    
    static Transform from_string(const std::string& input)
    {
        
        glm::vec3 location;
        glm::vec3 rotation;
        glm::vec3 scale;
    
        char ch;
        std::stringstream ss(input);

        ss >> ch >> ch
           >> location.x >> ch >> location.y >> ch >> location.z
           >> ch >> ch
           >> rotation.x >> ch >> rotation.y >> ch >> rotation.z
           >> ch >> ch
           >> scale.x >> ch >> scale.y >> ch >> scale.z;

    
        return Transform::make_from_euler(location, rotation, scale);
    }
};

REFLECT_STRUCT(Transform, 
    position, rotation, scale);


inline bool convert_from_string(Transform& target, const std::string& value)
{
    if (!value.empty())
        target = Transform::from_string(value);
    return true;
}