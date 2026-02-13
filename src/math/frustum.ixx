export module rhmath:frustum;

import glm;
import :aabb;

export struct Frustum
{
    glm::vec4 planes[6];

    bool test_aabb(const AABB& bounds, const glm::mat4& world) const
    {
        glm::vec3 center = glm::vec3(world * glm::vec4(bounds.center(), 1.0f));

        glm::vec3 extents = bounds.get_extents();

        for (int i = 0; i < 6; ++i)
        {
            const glm::vec4& p = planes[i];
            glm::vec3 normal = glm::vec3(p);

            float r =
                extents.x * std::abs(normal.x) +
                extents.y * std::abs(normal.y) +
                extents.z * std::abs(normal.z);

            float d = glm::dot(normal, center) + p.w;

            if (d + r < 0.0f)
                return false;
        }

        return true;
    }
    
    void normalize_plane(glm::vec4& p)
    {
        float length = glm::length(glm::vec3(p));
        p /= length;
    }
    
    static Frustum from_view_projection(const glm::mat4& vp)
    {
        Frustum f;

        // left
        f.planes[0] = vp[3] + vp[0];
        // right
        f.planes[1] = vp[3] - vp[0];
        // down
        f.planes[2] = vp[3] + vp[1];
        // up
        f.planes[3] = vp[3] - vp[1];
        // Near
        f.planes[4] = vp[3] + vp[2];
        // Far
        f.planes[5] = vp[3] - vp[2];

        for (int i = 0; i < 6; ++i)
            f.normalize_plane(f.planes[i]);

        return f;
    }
};
