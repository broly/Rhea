#ifndef RESOURCES_HDR_COLOR_STORAGE
#define RESOURCES_HDR_COLOR_STORAGE

#include "hdr_color_defines.glsl"

#ifndef SET_HDR_COLOR_STORAGE
    #define SET_HDR_COLOR_STORAGE 0 
    #error "SET_HDR_COLOR_STORAGE definition is missing. Provide this resource: hdr_color_storage"
#endif
#ifndef BINDING_SAMPLER_HDR_COLOR_STORAGE
    #define BINDING_SAMPLER_HDR_COLOR_STORAGE 0 
    #error "BINDING_SAMPLER_HDR_COLOR_STORAGE definition is missing. Provide this resource: hdr_color_storage"
#endif

layout(set = SET_HDR_COLOR_STORAGE, binding = BINDING_SAMPLER_HDR_COLOR_STORAGE, rgba16f)
uniform image2D u_hdr_color_present_storage[NUM_COLOR_OUTPUTS];

#endif  // RESOURCES_HDR_COLOR_STORAGE