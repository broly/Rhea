#ifndef RESOURCES_GBUFFER
#define RESOURCES_GBUFFER

layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_NORMAL) uniform sampler2D u_normal;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_ROUGHNESS) uniform sampler2D u_roughness;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_DEPTH) uniform sampler2D u_depth;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_ALBEDO) uniform sampler2D u_albedo;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_POSITION) uniform sampler2D u_position;

#endif  // RESOURCES_GBUFFER