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
    uint32_t mesh_id;
    uint32_t primitive_id;
    uint32_t material_id;
    uint32_t debug_id;
};
REFLECT_STRUCT_RUNTIME(ModelPushConstants,
    mesh_id, 
    primitive_id, 
    material_id,
    debug_id);


export struct PushRTXGIValidate
{
    glm::vec2 resolution;
    float position_threshold;
    float normal_threshold;
};
REFLECT_STRUCT_RUNTIME(PushRTXGIValidate,
    resolution, 
    position_threshold, 
    normal_threshold);


export struct TemporalAccumPC
{
    bool reset;
};
REFLECT_STRUCT_RUNTIME(TemporalAccumPC,
    reset);


export struct PushRTXGISpatial
{
    glm::vec2 resolution;
};
REFLECT_STRUCT_RUNTIME(PushRTXGISpatial,
    resolution);


export struct RTXGIPushConstants
{
    uint32_t frame;
    float intensity;
    uint32_t spp;
};
REFLECT_STRUCT_RUNTIME(RTXGIPushConstants,
    frame, intensity);