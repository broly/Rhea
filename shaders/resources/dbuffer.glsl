#ifndef RESOURCES_DBUFFER
#define RESOURCES_DBUFFER

#ifndef SET_DBUFFER
    #define SET_DBUFFER 0
    #error "SET_DBUFFER definition is missing"
#endif
#ifndef BINDING_SAMPLER_DECAL_ALBEDO
    #define BINDING_SAMPLER_DECAL_ALBEDO 0
    #error "BINDING_SAMPLER_DECAL_ALBEDO definition is missing. Provide this resource: gbuffer"
#endif

layout(set = SET_DBUFFER, binding = BINDING_SAMPLER_DECAL_ALBEDO) 
uniform sampler2D u_decal_albedo;

#endif  // RESOURCES_DBUFFER