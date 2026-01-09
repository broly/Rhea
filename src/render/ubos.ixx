export module render:ubos;

import glm;

export struct CameraUBO
{
    glm::mat4 view_proj;
    glm::vec3 camera_pos;
    float pad;
};

export struct ModelUBO_Temp
{
    glm::mat4 position;
    float pad;
};
