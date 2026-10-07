#ifndef RESOURCES_JPEG
#define RESOURCES_JPEG

// Shakalizer, the GPU JPEG (game: JpegRenderer, src/game/jpeg.cpp). One resource for every pass, an instance
// (descriptor set) per pass of a chain: each shader uses only the bindings of its pass.
//   jpeg_downscale.comp   u_jpeg_source (any size, sampled) -> u_jpeg_work (1 / downscale of it, display 8 bit)
//   jpeg_codec.comp       u_jpeg_work in place: one JPEG generation (YCbCr, subsampling, DCT, quantization)
//   jpeg_output.comp      u_jpeg_work -> u_jpeg_target (the size of the source): upscale, sharpen
//   jpeg_hit_mask.comp    jpeg_hits_ubo (world spheres), the depth -> u_jpeg_mask (RGBA8, one texel per 8 x 8
//                         block, a channel per hit slot)
//   jpeg_blocks.comp      u_jpeg_mask -> u_jpeg_blocks (resource jpeg_blocks): the MCUs a masked chain codes
//   jpeg_composite.comp   u_jpeg_source (the frame), u_jpeg_result (its shakalized copy), u_jpeg_hit_result and
//                         u_jpeg_mask_sampled (the hits) -> u_jpeg_target
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
#ifndef BINDING_UBO_JPEG_HITS
    #define BINDING_UBO_JPEG_HITS 0
    #error "BINDING_UBO_JPEG_HITS definition is missing. Provide this resource: jpeg"
#endif
#ifndef BINDING_IMAGE_JPEG_MASK
    #define BINDING_IMAGE_JPEG_MASK 0
    #error "BINDING_IMAGE_JPEG_MASK definition is missing. Provide this resource: jpeg"
#endif
#ifndef BINDING_SAMPLER_JPEG_MASK
    #define BINDING_SAMPLER_JPEG_MASK 0
    #error "BINDING_SAMPLER_JPEG_MASK definition is missing. Provide this resource: jpeg"
#endif
#ifndef BINDING_SAMPLER_JPEG_HIT_RESULT
    #define BINDING_SAMPLER_JPEG_HIT_RESULT 0
    #error "BINDING_SAMPLER_JPEG_HIT_RESULT definition is missing. Provide this resource: jpeg"
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
#define JPEG_FLAG_BLOCK_LIST 4      // masked chain: the codec runs over the MCUs of u_jpeg_blocks (indirect), the
                                    // output writes only the blocks its mask channel wins
#define JPEG_FLAG_CHANNEL_SHIFT 8   // bits 8..9: the channel of u_jpeg_mask of a masked chain

// jpeg_hits::max_hits (src/game/jpeg_hits.ixx)
#define JPEG_MAX_HITS 64
// pixels per texel of u_jpeg_mask
#define JPEG_MASK_BLOCK 8

layout(set = SET_JPEG, binding = BINDING_SAMPLER_JPEG_SOURCE)
uniform sampler2D u_jpeg_source;

// a fragment shader may define JPEG_NO_IMAGES: it neither reads nor writes storage images
#ifndef JPEG_NO_IMAGES
layout(set = SET_JPEG, binding = BINDING_IMAGE_JPEG_WORK, rgba8)
uniform image2D u_jpeg_work;

layout(set = SET_JPEG, binding = BINDING_IMAGE_JPEG_TARGET, rgba8)
uniform writeonly image2D u_jpeg_target;

// strength of the hits per 8 x 8 block of the screen, a channel per hit slot (JpegHitSlots), 0: none
layout(set = SET_JPEG, binding = BINDING_IMAGE_JPEG_MASK, rgba8)
uniform image2D u_jpeg_mask;
#endif

layout(set = SET_JPEG, binding = BINDING_SAMPLER_JPEG_MASK)
uniform sampler2D u_jpeg_mask_sampled;

layout(set = SET_JPEG, binding = BINDING_SAMPLER_JPEG_HIT_RESULT)
uniform sampler2D u_jpeg_hit_result;

// JpegHitsUBO (src/game/jpeg_hits.ixx): the live hit spheres
layout(set = SET_JPEG, binding = BINDING_UBO_JPEG_HITS) uniform JpegHitsUBO
{
    vec4 spheres[JPEG_MAX_HITS];    // xyz: center (world), w: radius
    vec4 params[JPEG_MAX_HITS];     // x: strength now (faded by age), y: seed of the ragged edge, z: slot (mask
                                    // channel), w: ragged edge, share of the radius
    uvec4 info;                     // x: count, y: channel + 1 of the bodies under a JPEG (0: none)
} jpeg_hits_ubo;

layout(set = SET_JPEG, binding = BINDING_SAMPLER_JPEG_RESULT)
uniform sampler2D u_jpeg_result;

// JpegPushConstants (src/game/jpeg.ixx), shared by the passes of a chain
layout(push_constant) uniform JpegPushConstants
{
    ivec4 extent;   // xy: the used part of u_jpeg_work (source / downscale, rounded up), zw: the source and target
    ivec4 mode;     // x: downscale factor (jpeg_factor), y: JPEG_FILTER_*, z: JPEG_CHROMA_*, w: JPEG_FLAG_*
    ivec4 codec;    // x: quality 1..100, yz: grid shift of this generation (px of u_jpeg_work), w: generation
    vec4 post;      // x: sharpen, y: downscale: brightness gain, composite: mix of the full screen damage,
                    // z: codec with JPEG_FLAG_BLOCK_LIST: quality where the mask is weak (pc.codec.x where it is 1),
                    // composite: 1 shows the hits, w: downscale: noise amplitude
                    // codec.y in the downscale: noise seed (frame)
    // composite only (JpegCompositeParams)
    vec4 vignette;  // x: inner radius, y: feather, z: block px (0: smooth), w: dither seed
    vec4 tint;      // rgb: red edge color x strength, w: its inner radius
} pc;

// the downscale factor per axis (mode.x: x | y << 8)
ivec2 jpeg_factor()
{
    return max(ivec2(pc.mode.x & 0xff, (pc.mode.x >> 8) & 0xff), ivec2(1));
}

// the channel of u_jpeg_mask a masked chain codes
int jpeg_mask_channel()
{
    return (pc.mode.w >> JPEG_FLAG_CHANNEL_SHIFT) & 3;
}

// the active MCUs of a masked chain (resource jpeg_blocks, jpeg_blocks.comp); only compute shaders see it
#ifdef BINDING_SSBO_JPEG_BLOCKS
layout(set = SET_JPEG_BLOCKS, binding = BINDING_SSBO_JPEG_BLOCKS, std430) buffer JpegBlocks
{
    uvec4 dispatch;     // VkDispatchIndirectCommand of the codec: x the MCU count, y = z = 1
    uvec2 mcus[];       // x: MCU x | MCU y << 16, y: the strongest mask under the MCU (float bits)
} u_jpeg_blocks;
#endif

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
