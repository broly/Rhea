export module rhmath:frustum;

import glm;
import :aabb;

export struct Frustum
{
    glm::vec4 planes[6];

    bool test_aabb_world(const AABB& bounds) const;


    void normalize_plane(glm::vec4& p)
    {
        float length = glm::length(glm::vec3(p));
        p /= length;
    }
    
    static Frustum from_view_projection(const glm::mat4& m);
};
