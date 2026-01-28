export module rhmath:vertex;

import :vec;
#include "common/reflect_macros.h"

export struct Vertex 
{
    vec3 position;
    vec3 normal;
    vec2 tex_coord;
    vec4 tangent;
};
REFLECT_STRUCT_RUNTIME(Vertex,
    position, normal, tex_coord, tangent);