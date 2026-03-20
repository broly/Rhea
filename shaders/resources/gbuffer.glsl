#ifndef RESOURCES_GBUFFER
#define RESOURCES_GBUFFER

#ifndef SET_GBUFFER
    #define SET_GBUFFER 0
    #error "SET_GBUFFER must be provided"
#endif
#ifndef BINDING_SAMPLER_NORMAL
    #define BINDING_SAMPLER_NORMAL 0
    #error "BINDING_SAMPLER_NORMAL must be provided"
#endif
#ifndef BINDING_SAMPLER_WORLD_NORMAL
    #define BINDING_SAMPLER_WORLD_NORMAL 0
    #error "BINDING_SAMPLER_WORLD_NORMAL must be provided"
#endif
#ifndef BINDING_SAMPLER_ROUGHNESS
    #define BINDING_SAMPLER_ROUGHNESS 0
    #error "BINDING_SAMPLER_ROUGHNESS must be provided"
#endif
#ifndef BINDING_SAMPLER_DEPTH
    #define BINDING_SAMPLER_DEPTH 0
    #error "BINDING_SAMPLER_DEPTH must be provided"
#endif
#ifndef BINDING_SAMPLER_ALBEDO
    #define BINDING_SAMPLER_ALBEDO 0
    #error "BINDING_SAMPLER_ALBEDO must be provided"
#endif
#ifndef BINDING_SAMPLER_POSITION
    #define BINDING_SAMPLER_POSITION 0
    #error "BINDING_SAMPLER_POSITION must be provided"
#endif

layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_NORMAL) uniform sampler2D u_normal;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_WORLD_NORMAL) uniform sampler2D u_world_normal;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_ROUGHNESS) uniform sampler2D u_roughness;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_DEPTH) uniform sampler2D u_depth;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_ALBEDO) uniform sampler2D u_albedo;
layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_POSITION) uniform sampler2D u_position;

#endif  // RESOURCES_GBUFFER