#ifndef RESOURCES_HDR_COLOR
#define RESOURCES_HDR_COLOR

layout(set = SET_HDR_COLOR, binding = BINDING_SAMPLER_HDR_COLOR) uniform sampler2D u_hdr_color;
layout(set = SET_HDR_COLOR, binding = BINDING_SAMPLER_HISTORY) uniform sampler2D u_history;

#endif  // RESOURCES_HDR_COLOR