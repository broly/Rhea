export module render:ubos;

import glm;
import rhmath;
#include "common/reflect_macros.h"

#define PAD_COMBINE_INNER(a,b) a##b

#define PAD_COMBINE(a,b) PAD_COMBINE_INNER(a,b)


#define PAD_INNER(v) float PAD_COMBINE(__pad,__LINE__)[v] = {}

#define PAD(v) PAD_INNER(v)


export struct CameraUBO
{
    glm::mat4 proj;
    glm::mat4 view;
    
    glm::mat4 prev_proj;
    glm::mat4 prev_view;
    
    glm::mat4 inv_view;
    glm::mat4 inv_proj;
    glm::mat4 inv_viewproj;
    
    glm::vec3 camera_pos;   PAD(1);
    float near;
    float far;
};
REFLECT_STRUCT_RUNTIME(CameraUBO,
    proj, view, prev_proj, prev_view, inv_view, inv_proj, inv_viewproj, camera_pos, near, far);


export struct ModelPushConstants
{
    glm::mat4 model;
    glm::mat4 prev_model;
    uint32_t mesh_index;
};
REFLECT_STRUCT_RUNTIME(ModelPushConstants,
    model, prev_model, mesh_index);




export struct RTXGIPushConstants
{
    uint32_t frame;
    float intensity;
};
REFLECT_STRUCT_RUNTIME(RTXGIPushConstants,
    frame, intensity);