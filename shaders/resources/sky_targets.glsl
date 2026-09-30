#ifndef RESOURCES_SKY_TARGETS
#define RESOURCES_SKY_TARGETS

// The sky buffers of this frame as the targets of sky_march.comp (layout: resources/sky_buffers.glsl)

#ifndef SET_SKY_TARGETS
    #define SET_SKY_TARGETS 0
    #error "SET_SKY_TARGETS definition is missing. Provide this resource: sky_targets"
#endif
#ifndef BINDING_IMAGE_ATMOSPHERE_TARGET
    #define BINDING_IMAGE_ATMOSPHERE_TARGET 0
    #error "BINDING_IMAGE_ATMOSPHERE_TARGET definition is missing. Provide this resource: sky_targets"
#endif
#ifndef BINDING_IMAGE_CLOUDS_TARGET
    #define BINDING_IMAGE_CLOUDS_TARGET 0
    #error "BINDING_IMAGE_CLOUDS_TARGET definition is missing. Provide this resource: sky_targets"
#endif

layout(set = SET_SKY_TARGETS, binding = BINDING_IMAGE_ATMOSPHERE_TARGET, rgba16f)
uniform writeonly image2D u_atmosphere_target;

layout(set = SET_SKY_TARGETS, binding = BINDING_IMAGE_CLOUDS_TARGET, rgba16f)
uniform writeonly image2D u_clouds_target;

#endif // RESOURCES_SKY_TARGETS
