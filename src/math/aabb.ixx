export module rhmath:aabb;
import glm;
import <type_traits>;

export class AABB
{
public:
    
    glm::vec3 min;
    glm::vec3 max;
};

static_assert(std::is_move_constructible_v<AABB>);
static_assert(std::is_nothrow_move_constructible_v<AABB>);
static_assert(std::is_trivially_move_constructible_v<AABB>);
