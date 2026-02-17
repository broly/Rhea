export module render:ubos;

import glm;
import rhmath;
#include "common/reflect_macros.h"


export struct CameraUBO
{
    glm::mat4 proj;
    glm::mat4 view;
    glm::vec3 camera_pos;
    float _pad = 0.f;
};
REFLECT_STRUCT_RUNTIME(CameraUBO,
    proj, view, camera_pos);
