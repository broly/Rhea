#ifndef RESOURCES_GBUFFER
#define RESOURCES_GBUFFER

layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_NORMAL) uniform sampler2D u_normal;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_ROUGHNESS) uniform sampler2D u_roughness;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_DEPTH) uniform sampler2D u_depth;

#endif  // RESOURCES_GBUFFER