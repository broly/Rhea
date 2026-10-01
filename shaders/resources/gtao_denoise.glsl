#ifndef RESOURCES_GTAO_DENOISE
#define RESOURCES_GTAO_DENOISE

// Source and target of one gtao_denoise.comp pass: resources gtao_denoise_1 (raw -> mid) and gtao_denoise_2
// (mid -> result) share these names (layout: resources/gtao.glsl)

#ifndef SET_GTAO_DENOISE
    #define SET_GTAO_DENOISE 0
    #error "SET_GTAO_DENOISE definition is missing. Provide this resource: gtao_denoise_1 or gtao_denoise_2"
#endif
#ifndef BINDING_SAMPLER_GTAO_DENOISE_SOURCE
    #define BINDING_SAMPLER_GTAO_DENOISE_SOURCE 0
    #error "BINDING_SAMPLER_GTAO_DENOISE_SOURCE definition is missing. Provide this resource: gtao_denoise_1 or gtao_denoise_2"
#endif
#ifndef BINDING_IMAGE_GTAO_DENOISE_TARGET
    #define BINDING_IMAGE_GTAO_DENOISE_TARGET 0
    #error "BINDING_IMAGE_GTAO_DENOISE_TARGET definition is missing. Provide this resource: gtao_denoise_1 or gtao_denoise_2"
#endif

layout(set = SET_GTAO_DENOISE, binding = BINDING_SAMPLER_GTAO_DENOISE_SOURCE)
uniform sampler2D u_gtao_denoise_source;

layout(set = SET_GTAO_DENOISE, binding = BINDING_IMAGE_GTAO_DENOISE_TARGET, rgba16f)
uniform writeonly image2D u_gtao_denoise_target;

#endif // RESOURCES_GTAO_DENOISE
