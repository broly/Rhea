export module rhmath:vec;

import glm;
import reflect;

#include "common/reflect_macros.h"

export struct vec2
{
    float x, y;
    
    using glm_type = glm::vec2;
    
    vec2(float x = 0.f, float y = 0.f)
        : x(x), y(y)
    {}
    
    vec2(glm_type vec)
        : x(vec.x), y(vec.y)
    {}
    
    vec2 operator=(glm_type vec)
    {
        x = vec.x;
        y = vec.y;
        return *this;
    }
    
    operator glm_type() const
    {
        return glm_type(x, y);
    }
};
REFLECT_STRUCT(vec2,
    x, y);


export struct vec3
{
    float x, y, z;
    
    using glm_type = glm::vec3;
    
    vec3(float x = 0.f, float y = 0.f, float z = 0.f)
        : x(x), y(y), z(z)
    {}
    
    vec3(glm_type vec)
        : x(vec.x), y(vec.y), z(vec.z)
    {}
    
    vec3 operator=(glm_type vec)
    {
        x = vec.x;
        y = vec.y;
        z = vec.z;
        return *this;
    }
    
    operator glm_type() const
    {
        return glm_type(x, y, z);
    }
    
    auto glm() const
    {
        return glm_type(x, y, z);
    }
};
REFLECT_STRUCT(vec3,
    x, y, z);

export struct vec4
{
    float x, y, z, w;
    
    using glm_type = glm::vec4;
    
    vec4(float x = 0.f, float y = 0.f, float z = 0.f, float w = 0.f)
        : x(x), y(y), z(z), w(w)
    {}
    
    vec4(glm_type vec)
        : x(vec.x), y(vec.y), z(vec.z), w(vec.w)
    {}
    
    vec4 operator=(glm_type vec)
    {
        x = vec.x;
        y = vec.y;
        z = vec.z;
        w = vec.w;
        return *this;
    }
    
    operator glm_type() const
    {
        return glm_type(x, y, z, w);
    }
};
REFLECT_STRUCT(vec4,
    x, y, z, w);

export struct quat
{
    float x, y, z, w;
    
    using glm_type = glm::quat;
    
    quat(float x = 0.f, float y = 0.f, float z = 0.f, float w = 0.f)
        : x(x), y(y), z(z), w(w)
    {}
    
    quat(glm_type q)
        : x(q.x), y(q.y), z(q.z), w(q.w)
    {}
    
    quat operator=(glm_type q)
    {
        x = q.x;
        y = q.y;
        z = q.z;
        w = q.w;
        return *this;
    }
    
    operator glm_type() const
    {
        return glm_type(w, x, y, z);
    }
    
    auto glm() const
    {
        return glm_type(w, x, y, z);
    }
};
REFLECT_STRUCT(quat,
    x, y, z, w);