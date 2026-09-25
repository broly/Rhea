export module linear_color;

import std.compat;
import fixed_string;
import reflect;
import type_id;

import glm;

export struct LinearColor
{
    float r, g, b, a;
    
    LinearColor(float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f)
        : r(r), g(g), b(b), a(a)
    {
    }
    
    LinearColor(glm::vec3 vec)
        : r(vec.x), g(vec.y), b(vec.z), a(1.0f)
    {
    }
    
    LinearColor(glm::vec4 vec)
        : r(vec.x), g(vec.y), b(vec.z), a(vec.w)
    {
    }
};
