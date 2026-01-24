export module rhmath:aabb;
import glm;
import <type_traits>;
import :transform;
import <float.h>;

export class AABB
{
public:
    
    AABB(glm::vec3 min = glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3 max = glm::vec3(0.0f, 0.0f, 0.0f))
            : min(min)
            , max(max)
    {}
    
    AABB operator*(const Transform& t) const
    {
        return transform(t);
    }
    
    
    AABB& operator*=(const Transform& t)
    {
        *this = transform(t);
        
        return *this;
    }
    
    AABB operator+(const AABB& other) const
    {
        return combine(*this, other);
    }
    
    AABB& operator+=(const AABB& other)
    {
        *this = combine(*this, other);
        return *this;
    }
    
    static AABB combine(const AABB& a, const AABB& b) {
        AABB result;
        result.min = glm::min(a.min, b.min);
        result.max = glm::max(a.max, b.max);
        return result;
    }
    
    AABB transform(const Transform& t) const
    {
        glm::mat4 m = t.matrix();

        glm::vec3 corners[8] = {
            {min.x, min.y, min.z},
            {max.x, min.y, min.z},
            {min.x, max.y, min.z},
            {max.x, max.y, min.z},
            {min.x, min.y, max.z},
            {max.x, min.y, max.z},
            {min.x, max.y, max.z},
            {max.x, max.y, max.z}
        };

        glm::vec3 world_min(FLT_MAX);
        glm::vec3 world_max(-FLT_MAX);

        for (int i = 0; i < 8; ++i) {
            glm::vec3 transformed = glm::vec3(m * glm::vec4(corners[i], 1.0f));
            world_min = glm::min(world_min, transformed);
            world_max = glm::max(world_max, transformed);
        }

        return {world_min, world_max};
    }
    
    glm::vec3 center() const
    {
        return (min + max) * 0.5f;
    }

    glm::vec3 radius() const
    {
        return (max - min) * 0.5f;
    }
    
    float scalar_radius() const
    {
        return glm::length(max - min) * 0.5f;
    }
    
    void get_corners(glm::vec3 out[8]) const
    {
        out[0] = { min.x, min.y, min.z };
        out[1] = { max.x, min.y, min.z };
        out[2] = { min.x, max.y, min.z };
        out[3] = { max.x, max.y, min.z };

        out[4] = { min.x, min.y, max.z };
        out[5] = { max.x, min.y, max.z };
        out[6] = { min.x, max.y, max.z };
        out[7] = { max.x, max.y, max.z };
    }

    
    glm::vec3 min;
    glm::vec3 max;
};

static_assert(std::is_move_constructible_v<AABB>);
static_assert(std::is_nothrow_move_constructible_v<AABB>);
static_assert(std::is_trivially_move_constructible_v<AABB>);
