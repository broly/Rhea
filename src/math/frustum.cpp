module rhmath:frustum;

bool Frustum::test_aabb_world(const AABB& bounds) const
{
    for (int i = 0; i < 6; ++i)
    {
        const glm::vec3 normal = glm::vec3(planes[i]);
        const float d = planes[i].w;

        glm::vec3 positive;

        positive.x = (normal.x >= 0.0f) ? bounds.max.x : bounds.min.x;
        positive.y = (normal.y >= 0.0f) ? bounds.max.y : bounds.min.y;
        positive.z = (normal.z >= 0.0f) ? bounds.max.z : bounds.min.z;

        if (glm::dot(normal, positive) + d < 0.0f)
            return false;
    }

    return true; 
}

Frustum Frustum::from_view_projection(const glm::mat4& m)
{
    Frustum f;

    // Left
    f.planes[0] = glm::vec4(
        m[0][3] + m[0][0],
        m[1][3] + m[1][0],
        m[2][3] + m[2][0],
        m[3][3] + m[3][0]);

    // Right
    f.planes[1] = glm::vec4(
        m[0][3] - m[0][0],
        m[1][3] - m[1][0],
        m[2][3] - m[2][0],
        m[3][3] - m[3][0]);

    // Bottom
    f.planes[2] = glm::vec4(
        m[0][3] + m[0][1],
        m[1][3] + m[1][1],
        m[2][3] + m[2][1],
        m[3][3] + m[3][1]);

    // Top
    f.planes[3] = glm::vec4(
        m[0][3] - m[0][1],
        m[1][3] - m[1][1],
        m[2][3] - m[2][1],
        m[3][3] - m[3][1]);

    // Near (Vulkan RH_ZO → z ∈ [0,1])
    f.planes[4] = glm::vec4(
        m[0][2],
        m[1][2],
        m[2][2],
        m[3][2]);

    // Far
    f.planes[5] = glm::vec4(
        m[0][3] - m[0][2],
        m[1][3] - m[1][2],
        m[2][3] - m[2][2],
        m[3][3] - m[3][2]);

    for (int i = 0; i < 6; ++i)
        f.normalize_plane(f.planes[i]);

    return f;
}
