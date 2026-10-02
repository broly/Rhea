#ifndef RESOURCES_JPEG
#define RESOURCES_JPEG

// Shakalizer, the GPU JPEG (game: JpegRenderer, src/game/jpeg.cpp). One resource for every pass, an instance
// (descriptor set) per pass of a chain: each shader uses only the bindings of its pass.
//   jpeg_downscale.comp   u_jpeg_source (any size, sampled) -> u_jpeg_work (1 / downscale of it, display 8 bit)
//   jpeg_codec.comp       u_jpeg_work in place: one JPEG generation (YCbCr, subsampling, DCT, quantization)
//   jpeg_output.comp      u_jpeg_work -> u_jpeg_target (the size of the source): upscale, sharpen
//   jpeg_present.frag     u_jpeg_source (the frame) and u_jpeg_result (its shakalized copy) -> the swapchain
//
// u_jpeg_work holds display encoded 8 bit RGB in its top left corner (the rest is unused): a JPEG encoder
// sees the same values a JPEG file of the frame would hold.

#ifndef SET_JPEG
    #define SET_JPEG 0
    #error "SET_JPEG definition is missing. Provide this resource: jpeg"
#endif
#ifndef BINDING_SAMPLER_JPEG_SOURCE
    #define BINDING_SAMPLER_JPEG_SOURCE 0
    #error "BINDING_SAMPLER_JPEG_SOURCE definition is missing. Provide this resource: jpeg"
#endif
#ifndef BINDING_IMAGE_JPEG_WORK
    #define BINDING_IMAGE_JPEG_WORK 0
    #error "BINDING_IMAGE_JPEG_WORK definition is missing. Provide this resource: jpeg"
#endif
#ifndef BINDING_IMAGE_JPEG_TARGET
    #define BINDING_IMAGE_JPEG_TARGET 0
    #error "BINDING_IMAGE_JPEG_TARGET definition is missing. Provide this resource: jpeg"
#endif
#ifndef BINDING_SAMPLER_JPEG_RESULT
    #define BINDING_SAMPLER_JPEG_RESULT 0
    #error "BINDING_SAMPLER_JPEG_RESULT definition is missing. Provide this resource: jpeg"
#endif

// JpegChroma (src/game/jpeg.ixx)
#define JPEG_CHROMA_444 0
#define JPEG_CHROMA_422 1
#define JPEG_CHROMA_420 2
#define JPEG_CHROMA_411 3

// JpegFilter
#define JPEG_FILTER_NEAREST 0
#define JPEG_FILTER_BOX 1

// JpegPushConstants::mode.w
#define JPEG_FLAG_SOURCE_LINEAR 1   // the source holds linear color: encoded to sRGB before the codec
#define JPEG_FLAG_TARGET_LINEAR 2   // the target wants linear color: decoded from sRGB after the codec

layout(set = SET_JPEG, binding = BINDING_SAMPLER_JPEG_SOURCE)
uniform sampler2D u_jpeg_source;

// the fragment shader of the present pass defines JPEG_NO_IMAGES: it neither reads nor writes storage images
#ifndef JPEG_NO_IMAGES
layout(set = SET_JPEG, binding = BINDING_IMAGE_JPEG_WORK, rgba8)
uniform image2D u_jpeg_work;

layout(set = SET_JPEG, binding = BINDING_IMAGE_JPEG_TARGET, rgba8)
uniform writeonly image2D u_jpeg_target;
#endif

layout(set = SET_JPEG, binding = BINDING_SAMPLER_JPEG_RESULT)
uniform sampler2D u_jpeg_result;

// JpegPushConstants (src/game/jpeg.ixx), shared by the passes of a chain
layout(push_constant) uniform JpegPushConstants
{
    ivec4 extent;   // xy: the used part of u_jpeg_work (source / downscale, rounded up), zw: the source and target
    ivec4 mode;     // x: downscale factor, y: JPEG_FILTER_*, z: JPEG_CHROMA_*, w: JPEG_FLAG_*
    ivec4 codec;    // x: quality 1..100, yz: grid shift of this generation (px of u_jpeg_work), w: generation
    vec4 post;      // x: sharpen, y: present mix (jpeg_present.frag), zw: unused
} pc;

vec3 jpeg_linear_to_srgb(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

vec3 jpeg_srgb_to_linear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

#endif // RESOURCES_JPEG
