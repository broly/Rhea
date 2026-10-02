module;

#include <json/value.h>

export module game:render_graph;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render;
import glm;
import name;
import engine;
import assets;
import :generic_render_graph;
import :post_process;
import :jpeg;
import :screen_jpeg;
import :jpeg_hits;
#include "object/object_reflection_macro.h"

struct TonemapPushConstants
{
    float time;
    uint32_t mode;
    uint32_t fps;
    uint32_t view_mode;   // DebugViewMode
};
RH_REGISTER_TYPE(TonemapPushConstants)


class GameRenderGraph : public GenericRenderGraph
{
public:
    GameRenderGraph();
    void init_resources(const std::map<Name, bool>& init_params) override;
    void build_passes(const std::map<Name, bool>& parameters) override;
    void prepare_resources(RenderGraphContext& ctx) override;
    
    
    void pass_shadow_map(RenderGraphContext& ctx);
    void pass_shadow_debug(RenderGraphContext& ctx);
    void pass_translucent(RenderGraphContext& ctx);
    void pass_tonemapping(RenderGraphContext& ctx);
    void pass_readback(RenderGraphContext& ctx);

    std::unique_ptr<PostProcessRenderer> post_renderer;
    std::unique_ptr<JpegRenderer> jpeg_renderer;

private:
    // bloom chain and auto exposure between TAA and the tone mapping (PostProcessRenderer)
    void add_post_process_passes();

    // 1/2 .. 1/64 of the screen
    std::array<RGTextureHandle, PostProcessRenderer::bloom_levels> bloom_down;
    // 1/2 .. 1/32: bloom_up[0] is the bloom the tone mapping adds
    std::array<RGTextureHandle, PostProcessRenderer::bloom_levels - 1> bloom_up;
    // adapted exposure and the luminance histogram (resources/post_process.glsl)
    RGTextureHandle exposure;

    // the tone mapped frame (display encoded): pass Present copies it to the swapchain, mixed with screen_jpeg_color
    RGTextureHandle ldr_color;
    // ldr_color through the shakalizer (chain ScreenJpeg): the full screen JPEG damage
    RGTextureHandle screen_jpeg_color;
    screen_jpeg::State screen_jpeg_state;
    // JPEG spots of the hits (jpeg_hits): strength per 8 x 8 block of the screen and hit slot (RGBA8, pass
    // JpegHitMask); ldr_color through the masked chains HitJpeg0..3 (a slot each: only the MCUs under its channel
    // are coded, the blocks it wins written to hit_jpeg_color). The chains share their work texture.
    RGTextureHandle hit_mask;
    RGTextureHandle hit_jpeg_work;
    RGTextureHandle hit_jpeg_color;
    jpeg_hits::Frame hits_frame;
    // render.jpeg.hits.shoot: a hit where the camera looks
    void shoot_jpeg_hit(const jpeg_hits::ShootRequest& request);

    
    
    //bool capture_ibl;
    
};
RH_OBJECT(GameRenderGraph)