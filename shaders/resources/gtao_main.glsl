#ifndef RESOURCES_GTAO_MAIN
#define RESOURCES_GTAO_MAIN

// Target of gtao.comp (layout: resources/gtao.glsl)

#ifndef SET_GTAO_MAIN
    #define SET_GTAO_MAIN 0
    #error "SET_GTAO_MAIN definition is missing. Provide this resource: gtao_main"
#endif
#ifndef BINDING_IMAGE_GTAO_RAW_TARGET
    #define BINDING_IMAGE_GTAO_RAW_TARGET 0
    #error "BINDING_IMAGE_GTAO_RAW_TARGET definition is missing. Provide this resource: gtao_main"
#endif

layout(set = SET_GTAO_MAIN, binding = BINDING_IMAGE_GTAO_RAW_TARGET, rgba16f)
uniform writeonly image2D u_gtao_raw_target;

#endif // RESOURCES_GTAO_MAIN
