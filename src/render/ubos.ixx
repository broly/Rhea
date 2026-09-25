export module render:ubos;

import std.compat;
import fixed_string;
import reflect;
import type_id;

import glm;
import rhmath;
#include "common/reflect_macros.h"

#define PAD_COMBINE_INNER(a,b) a##b

#define PAD_COMBINE(a,b) PAD_COMBINE_INNER(a,b)


#define PAD_INNER(v) [[=rh::padding]] float PAD_COMBINE(__pad,__LINE__)[v] = {}

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
RH_REGISTER_TYPE(CameraUBO)


export struct ModelPushConstants
{
    uint32_t mesh_id;
    uint32_t primitive_id;
    uint32_t material_id;
    uint32_t debug_id;
};
RH_REGISTER_TYPE(ModelPushConstants)


export struct PushRTXGIValidate
{
    glm::vec2 resolution;
    float position_threshold;
    float normal_threshold;
};
RH_REGISTER_TYPE(PushRTXGIValidate)


export struct TemporalAccumPC
{
    bool reset;
    [[=rh::padding]] uint8_t pad[3] = {0,0,0};
};
RH_REGISTER_TYPE(TemporalAccumPC)


export struct PushRTXGISpatial
{
    glm::vec2 resolution;
};
RH_REGISTER_TYPE(PushRTXGISpatial)


export struct RTXGIPushConstants
{
    uint32_t frame;
    float intensity;
    uint32_t spp;
};
RH_REGISTER_TYPE(RTXGIPushConstants)