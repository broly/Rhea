export module glm;

#include <glm/fwd.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/detail/type_quat.hpp>
#include <glm/detail/type_vec4.hpp>
#include <glm/detail/type_vec3.hpp>
#include <glm/detail/type_vec2.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/ext/matrix_transform.hpp>

export namespace glm
{
    using glm::vec2;
    using glm::vec3;
    using glm::vec4;
    using glm::quat;
    using glm::mat4_cast;
    using glm::mat4;
    using glm::translate;
    using glm::lookAt;
    using glm::scale;
    using glm::radians;
    using glm::min;
    using glm::max;
    using glm::operator*;
    using glm::operator/;
    using glm::operator+;
    using glm::operator-;
    using glm::operator==;
    using glm::operator!=;
    using glm::perspective;
    
    using glm::normalize;
    using glm::length;
    using glm::mat3;
    using glm::quat_cast;
}