#ifndef RESOURCES_HDR_COLOR
#define RESOURCES_HDR_COLOR

#include "hdr_color_defines.glsl"

#ifndef SET_HDR_COLOR
    #define SET_HDR_COLOR 0
    #error "SET_HDR_COLOR definition is missing. Provide this resource: hdr_color_output"
#endif
#ifndef BINDING_SAMPLER_HDR_COLOR
    #define BINDING_SAMPLER_HDR_COLOR 0
    #error "BINDING_SAMPLER_HDR_COLOR definition is missing. Provide this resource: hdr_color_output"
#endif
#ifndef BINDING_SAMPLER_HISTORY
    #define BINDING_SAMPLER_HISTORY 0
    #error "BINDING_SAMPLER_HISTORY definition is missing. Provide this resource: hdr_color_output"
#endif

layout(set = SET_HDR_COLOR, binding = BINDING_SAMPLER_HDR_COLOR) 
uniform sampler2D u_hdr_color[NUM_COLOR_OUTPUTS];

layout(set = SET_HDR_COLOR, binding = BINDING_SAMPLER_HISTORY) 
uniform sampler2D u_history[NUM_COLOR_OUTPUTS];

#endif  // RESOURCES_HDR_COLOR