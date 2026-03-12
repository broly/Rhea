export module rhmath:vertex;

import :vec;
#include "common/reflect_macros.h"

export struct Vertex
{
    glm::vec3 position;
    float pad0;

    glm::vec3 normal;
    float pad1;

    glm::vec2 tex_coord;
    glm::vec2 pad2;

    glm::vec4 tangent;
};
REFLECT_STRUCT_RUNTIME(Vertex,
    position, normal, tex_coord, tangent);


export struct LineVertex
{
    glm::vec3 position;
    float _pad;
    glm::vec4 color;
};
REFLECT_STRUCT_RUNTIME(LineVertex,
    position, color);