export module rhmath:vertex;

import glm;

export struct Vertex 
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec2 tex_coord;
};