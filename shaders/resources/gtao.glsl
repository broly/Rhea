#ifndef RESOURCES_GTAO
#define RESOURCES_GTAO

// Ambient occlusion of the frame (GtaoRenderer, after denoising) at 1 / downscale of the screen:
// r = visibility (1: open), g = linear depth of the pixel it was computed for (upsampling), b = packed edges

#ifndef SET_GTAO
    #define SET_GTAO 0
    #error "SET_GTAO definition is missing. Provide this resource: gtao"
#endif
#ifndef BINDING_SAMPLER_GTAO
    #define BINDING_SAMPLER_GTAO 0
    #error "BINDING_SAMPLER_GTAO definition is missing. Provide this resource: gtao"
#endif

layout(set = SET_GTAO, binding = BINDING_SAMPLER_GTAO)
uniform sampler2D u_gtao;

#endif // RESOURCES_GTAO
