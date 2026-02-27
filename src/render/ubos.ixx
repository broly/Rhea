export module render:ubos;

import glm;
import rhmath;
#include "common/reflect_macros.h"


export struct CameraUBO
{
    glm::mat4 proj;
    glm::mat4 view;
    glm::mat4 prev_proj;
    glm::mat4 prev_view;
    glm::vec3 camera_pos;
    float _pad0 = 0.f;
};
REFLECT_STRUCT_RUNTIME(CameraUBO,
    proj, view, prev_proj, prev_view,  camera_pos);


export struct ModelPushConstants
{
    glm::mat4 model;
    glm::mat4 prev_model;
};
REFLECT_STRUCT_RUNTIME(ModelPushConstants,
    model, prev_model);