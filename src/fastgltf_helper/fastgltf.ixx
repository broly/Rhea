export module fastgltf_helper;

import <type_traits>;

import fastgltf;

import glm;

export template<typename T>
auto fastgltf_to_glm(const T& v)
{
    if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, fastgltf::math::fvec3>)
    {
        return glm::vec3(v.x(), v.y(), v.z());
    } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, fastgltf::math::fvec2>)
    {
        return glm::vec2(v.x(), v.y());
    } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, fastgltf::math::fvec4>)
    {
        return glm::vec4(v.x(), v.y(), v.z(), v.w());
    } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, fastgltf::math::fquat>)
    {
        return glm::quat(v.w(), v.x(), v.y(), v.z());
    } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, fastgltf::math::fmat4x4>)
    {
        return glm::make_mat4(v.data());
    }
}
