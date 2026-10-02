#ifndef RESOURCES_POST_PROCESS
#define RESOURCES_POST_PROCESS

// Post processing (game: PostProcessRenderer, src/game/post_process.cpp). One resource for every pass, an
// instance (descriptor set) per pass: each shader uses only the bindings of its pass.
//   bloom_downsample.comp   u_pp_source (the bigger level) -> u_pp_target
//   bloom_upsample.comp     u_pp_source (the smaller level, upsampled) + u_pp_source_add (this level) -> u_pp_target
//   auto_exposure.comp      u_pp_source (a small bloom level) -> u_pp_exposure_target
//   tonemap.slang           post_process_ubo, u_pp_bloom, u_pp_exposure (resources/post_process.slang)
//
// u_pp_exposure_target / u_pp_exposure (RGBA32F, 64 x 2):
//   texel (0, 0): x adapted exposure (EV), y target exposure of this frame (EV), z average log2 luminance
//                 of the metered range, w 1 once written
//   row 1: the luminance histogram of this frame, bin i in texel (i, 1).x, scaled to the highest bin = 1

#ifndef SET_POST_PROCESS
    #define SET_POST_PROCESS 0
    #error "SET_POST_PROCESS definition is missing. Provide this resource: post_process"
#endif
#ifndef BINDING_SAMPLER_PP_SOURCE
    #define BINDING_SAMPLER_PP_SOURCE 0
    #error "BINDING_SAMPLER_PP_SOURCE definition is missing. Provide this resource: post_process"
#endif
#ifndef BINDING_SAMPLER_PP_SOURCE_ADD
    #define BINDING_SAMPLER_PP_SOURCE_ADD 0
    #error "BINDING_SAMPLER_PP_SOURCE_ADD definition is missing. Provide this resource: post_process"
#endif
#ifndef BINDING_IMAGE_PP_TARGET
    #define BINDING_IMAGE_PP_TARGET 0
    #error "BINDING_IMAGE_PP_TARGET definition is missing. Provide this resource: post_process"
#endif
#ifndef BINDING_IMAGE_PP_EXPOSURE_TARGET
    #define BINDING_IMAGE_PP_EXPOSURE_TARGET 0
    #error "BINDING_IMAGE_PP_EXPOSURE_TARGET definition is missing. Provide this resource: post_process"
#endif

#define PP_HISTOGRAM_BINS 64

layout(set = SET_POST_PROCESS, binding = BINDING_SAMPLER_PP_SOURCE)
uniform sampler2D u_pp_source;

layout(set = SET_POST_PROCESS, binding = BINDING_SAMPLER_PP_SOURCE_ADD)
uniform sampler2D u_pp_source_add;

layout(set = SET_POST_PROCESS, binding = BINDING_IMAGE_PP_TARGET, rgba16f)
uniform writeonly image2D u_pp_target;

layout(set = SET_POST_PROCESS, binding = BINDING_IMAGE_PP_EXPOSURE_TARGET, rgba32f)
uniform image2D u_pp_exposure_target;

#endif // RESOURCES_POST_PROCESS
