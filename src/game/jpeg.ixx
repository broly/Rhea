module;

#include <json/value.h>

export module game:jpeg;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render;
import glm;
import name;
import rhmath;
#include "object/object_reflection_macro.h"

// shaders/resources/jpeg.glsl, every pass of the shakalizer
struct JpegPushConstants
{
    glm::ivec4 extent;  // xy: the used part of the work texture, zw: the source (and target)
    glm::ivec4 mode;    // x: downscale factor (x | y << 8), y: JpegFilter, z: JpegChroma, w: jpeg_flag_*
    glm::ivec4 codec;   // x: quality, yz: grid shift of the generation, w: generation
    glm::vec4 post;     // x: sharpen, y: present mix, z: masked codec: quality of a weak mask, present: hits on
    // present only (JpegPresentParams)
    glm::vec4 vignette; // x: inner radius, y: feather, z: block px (0: smooth), w: dither seed
    glm::vec4 tint;     // rgb: edge color x strength, w: inner radius of the edge tint
    glm::vec4 hud;      // x: health, y: health lagging behind (the lost chunk), z: hit flash, w: bar scale (0 hidden)
};
RH_REGISTER_TYPE(JpegPushConstants)

// resource "jpeg", shaders/resources/jpeg.glsl (JPEG_MAX_HITS = jpeg_hits::max_hits): the live hit spheres
export struct JpegHitsUBO
{
    glm::vec4 spheres[64];  // xyz: center (world), w: radius
    glm::vec4 params[64];   // x: strength now (faded by age), y: seed of the ragged edge, z: slot, w: ragged edge
    glm::uvec4 info;        // x: count, y: slot + 1 of the bodies under a JPEG (jpeg_hits::hold_body; 0: none)
};
RH_REGISTER_TYPE(JpegHitsUBO)

// Keep in sync with JPEG_CHROMA_* in shaders/resources/jpeg.glsl
export enum class JpegChroma : uint8_t
{
    Yuv444,     // full color resolution
    Yuv422,     // half horizontally
    Yuv420,     // half both ways (what cameras and the web save)
    Yuv411,     // a quarter horizontally: color smears along the rows
};

// Keep in sync with JPEG_FILTER_*
export enum class JpegFilter : uint8_t
{
    Nearest,    // hard pixels
    Box,        // soap: the average, upscaled bilinearly
};

// One pass through the shakalizer: downscale -> JPEG (generations times, the grid shifted between them) ->
// upscale -> sharpen
export struct JpegSettings
{
    int downscale = 1;              // 1..jpeg::max_downscale
    int downscale_x = 0;            // horizontal factor, 1..jpeg::max_downscale; 0: downscale (clamped fills it)
    JpegFilter filter = JpegFilter::Box;
    int quality = 50;               // 1..100, as libjpeg
    JpegChroma chroma = JpegChroma::Yuv420;
    int generations = 1;            // 1..jpeg::max_generations
    float sharpen = 0.0f;           // 0..1
    uint32_t seed = 0;              // the grid shifts of the generations
    int mask_quality = 50;          // masked chains: quality where the mask is weak (quality where it is 1)
    // before the codec (masked chains: times the mask strength): brightness gain (0: none, 1: twice as bright)
    // and color noise amplitude (0..1)
    float brightness = 0.0f;
    float noise = 0.0f;
};

export namespace jpeg
{
    constexpr int max_downscale = 16;
    constexpr int max_generations = 16;

    // the settings in their ranges
    JpegSettings clamped(const JpegSettings& settings);

    // where the 8 x 8 grid of a generation starts (px of the work texture, 0..7 per axis): 0 for the first
    // one (an ordinary JPEG), 1..7 after it, so every recompression cuts the image elsewhere
    glm::ivec2 grid_shift(uint32_t seed, int generation);
}

// The pass Present (jpeg_present.frag): how the frame, its shakalized copy and the HUD come together
export struct JpegPresentParams
{
    // share of the shakalized frame over the whole screen, 0..1
    float amount = 0.0f;
    // and towards the edges: 0 inside radius (1: the half height of the screen, the horizontal axis squeezed by
    // sqrt(aspect), so a corner of 16:9 is at 1.67), 1 past radius + feather; radius above 2: off
    float vignette_radius = 10.0f;
    float vignette_feather = 0.3f;
    // > 0: the share is decided per block of this many px, dithered by seed (the edge breaks into blocks);
    // 0: smooth mix
    float block = 0.0f;
    uint32_t seed = 0;
    // red edges: linear-ish display color x strength, from radius (same units) to the corners
    glm::vec3 tint{ 0.0f };
    float tint_radius = 0.6f;
    // the JPEG spots of the hits (jpeg_hits) are on
    bool hits = false;
    // crosshair arm length in px, 0 hidden
    float crosshair = 0.0f;
    // health bar: fraction, the lagging fraction, hit flash 0..1, scale (0 hidden)
    float health = 1.0f;
    float health_lag = 1.0f;
    float health_flash = 0.0f;
    float health_bar = 0.0f;
};

// A shakalizer chain of a render graph: passes <name>Downscale, <name>Codec, <name>Output
export struct JpegChainDesc
{
    Name name;
    // sampled, any size and format
    RGTextureHandle source;
    // RGBA8_UNORM with storage usage, the size of the source
    RGTextureHandle target;
    // false skips the chain (target keeps its old content); nullptr: always
    std::function<bool()> condition;
    // read once a frame, before the passes
    std::function<JpegSettings()> settings;
    // the source holds linear color (textures, HDR): encoded to sRGB before the codec, as a JPEG file would be
    bool source_linear = false;
    // the target wants linear color: decoded from sRGB after the codec
    bool target_linear = false;
    // RGBA8 storage, one texel per 8 x 8 pixels of the source (JpegRenderer::add_hit_mask_pass): only the MCUs
    // under a set texel of mask_channel are coded (pass <name>Blocks, indirect dispatch), the quality follows its
    // strength (mask_quality -> quality), and the output writes only the 8 x 8 blocks this channel wins (the
    // strongest one): masked chains of the different channels share one target.
    std::optional<RGTextureHandle> mask;
    uint32_t mask_channel = 0;
    // a work texture shared with other chains (they run one after another; RGBA8_UNORM storage, the size of the
    // source); empty: the chain creates its own
    std::optional<RGTextureHandle> work;
};

// The shakalizer: real JPEG on the GPU (shaders/jpeg_*.comp, resource "jpeg", model "jpeg"). A chain shrinks
// the source into its work texture (RGBA8, display encoded, the size of the source), runs the JPEG codec on it
// in place generations times and writes the result upscaled to the target:
//   <name>Downscale   jpeg_downscale.comp  source -> work
//   <name>Codec       jpeg_codec.comp      work, one dispatch per generation (one workgroup per MCU)
//   <name>Output      jpeg_output.comp     work -> target, upscale and sharpen
// A masked chain adds <name>Blocks (jpeg_blocks.comp) before the codec: the MCUs under the mask -> the block
// list of resource jpeg_blocks (an instance per masked chain), which the codec dispatches indirectly.
// The codec is integer only (libjpeg's color conversion and quantization, a 13 bit DCT): the same bytes on
// every GPU. Every pass binds its own instance (descriptor set) of the resource.
export class JpegRenderer
{
public:
    void init(Renderer& renderer, RenderBackend& backend);

    // creates the work texture of the chain and adds its passes to the graph (from build_passes)
    void add_chain(RenderGraph& graph, JpegChainDesc desc);

    // descriptors and settings of every chain for the frame (from prepare_resources)
    void prepare(RenderGraphContext& ctx, RenderGraph& graph);

    // the settings a chain uses this frame (after prepare)
    const JpegSettings& get_chain_settings(Name name) const;

    // pass JpegHitMask (jpeg_hit_mask.comp): the hit spheres (JpegHitsUBO) -> mask (RGBA8, 1/8 of the screen, a
    // channel per hit slot) by the depth of the g-buffer, + the bodies under a JPEG (the level in the shading model
    // byte of geometry_normal). camera, gbuffer: the resources of the graph; depth: the linear depth texture
    void add_hit_mask_pass(RenderGraph& graph, RGTextureHandle mask, RGTextureHandle depth, RGTextureHandle geometry_normal,
        RenderResource* camera, RenderResource* gbuffer, std::function<bool()> condition);
    void prepare_hit_mask(RenderGraphContext& ctx, RBImageHandle mask, const JpegHitsUBO& ubo);

    // present pass (jpeg_present.frag): scene, mixed with result by amount, the blocks under hit_mask replaced
    // by hit_result, to the bound color attachment. Descriptors from prepare_present (prepare_resources), the
    // draw inside a graphics pass.
    void prepare_present(RenderGraphContext& ctx, RBImageHandle scene, RBImageHandle result, RBImageHandle hit_result,
        RBImageHandle hit_mask);
    void draw_present(RenderGraphContext& ctx, const JpegPresentParams& params);

private:
    struct Chain
    {
        JpegChainDesc desc;
        RGTextureHandle work;
        JpegSettings settings;
        uint32_t downscale_instance = 0;
        uint32_t codec_instance = 0;
        uint32_t output_instance = 0;
        // masked chains: the jpeg instance of the Blocks pass, the instance of jpeg_blocks
        uint32_t blocks_instance = 0;
        uint32_t block_list = 0;
    };

    Chain& find_chain(Name name);
    Extent source_extent(const RenderGraph& graph, const Chain& chain) const;
    JpegPushConstants make_push_constants(const RenderGraph& graph, const Chain& chain) const;

    void execute_downscale(RenderGraphContext& ctx, Chain& chain);
    void execute_blocks(RenderGraphContext& ctx, Chain& chain);
    void execute_codec(RenderGraphContext& ctx, Chain& chain);
    void execute_output(RenderGraphContext& ctx, Chain& chain);

    Renderer* renderer = nullptr;
    RenderBackend* backend = nullptr;
    RenderResource* resource = nullptr;
    RenderResource* blocks_resource = nullptr;

    std::shared_ptr<PipelineFamily> downscale_family;
    std::shared_ptr<PipelineFamily> codec_family;
    std::shared_ptr<PipelineFamily> output_family;
    std::shared_ptr<PipelineFamily> present_family;
    std::shared_ptr<PipelineFamily> blocks_family;
    std::shared_ptr<PipelineFamily> hit_mask_family;
    PipelineObject* downscale_pipeline = nullptr;
    PipelineObject* codec_pipeline = nullptr;
    PipelineObject* output_pipeline = nullptr;
    PipelineObject* present_pipeline = nullptr;
    PipelineObject* blocks_pipeline = nullptr;
    PipelineObject* hit_mask_pipeline = nullptr;

    // stable addresses: the passes keep pointers
    std::vector<std::unique_ptr<Chain>> chains;
    uint32_t next_instance = 0;
    uint32_t noise_frame = 0;
    uint32_t next_block_list = 0;
    std::optional<uint32_t> present_instance;
    std::optional<uint32_t> hit_mask_instance;
    RenderResource* hit_mask_camera = nullptr;
    RenderResource* hit_mask_gbuffer = nullptr;
};
