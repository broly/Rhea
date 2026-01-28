export module render:ubos;

import glm;
import rhmath;
#include "common/reflect_macros.h"

export struct CameraUBO
{
    glm::mat4 view_proj;
    glm::vec3 camera_pos;
    float pad;
};
REFLECT_STRUCT_RUNTIME_OPAQUE(CameraUBO);

export struct ModelUBO_Temp
{
    glm::mat4 position;
    float pad;
};
