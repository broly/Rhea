#pragma once
#include <glm/vec3.hpp>

class AABB
{
public:
    
    glm::vec3 min;
    glm::vec3 max;
};

static_assert(std::is_move_constructible_v<AABB>);
static_assert(std::is_nothrow_move_constructible_v<AABB>);
static_assert(std::is_trivially_move_constructible_v<AABB>);
