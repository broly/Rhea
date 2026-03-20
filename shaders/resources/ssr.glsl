#ifndef RESOURCES_SSR
#define RESOURCES_SSR

#ifndef SET_SSR
    #define SET_SSR 0
    #error "SET_SSR must be provided"
#endif
#ifndef BINDING_SAMPLER_SSR
    #define BINDING_SAMPLER_SSR 0
    #error "BINDING_SAMPLER_SSR must be provided"
#endif

layout(set = SET_SSR, binding = BINDING_SAMPLER_SSR) 
uniform sampler2D u_ssr;

#endif  // RESOURCES_SSR