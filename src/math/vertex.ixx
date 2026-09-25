export module rhmath:vertex;

import std.compat;
import fixed_string;
import reflect;
import type_id;

import :vec;
#include "common/reflect_macros.h"

export struct Vertex
{
    glm::vec3 position;
    [[=rh::padding]] float pad0;

    glm::vec3 normal;
    [[=rh::padding]] float pad1;

    glm::vec2 tex_coord;
    [[=rh::padding]] glm::vec2 pad2;

    glm::vec4 tangent;
};
RH_REGISTER_TYPE(Vertex)


export struct LineVertex
{
    glm::vec3 position;
    [[=rh::padding]] float _pad;
    glm::vec4 color;
};
RH_REGISTER_TYPE(LineVertex)