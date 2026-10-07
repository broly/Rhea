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
import :jpeg_hits;
import :damage_feedback;
import :hud;
import physics;
#include "object/object_reflection_macro.h"

struct TonemapPushConstants
{
    float time;
    uint32_t mode;
    uint32_t fps;
    uint32_t view_mode;   // DebugViewMode
};
RH_REGISTER_TYPE(TonemapPushConstants)

// shaders/present.frag
struct PresentPushConstants
{
    glm::vec4 hud;          // x: health, y: health lagging behind, z: hit flash, w: health bar scale (0 hidden)
    glm::vec4 crosshair;    // x: arm length (px), 0 hidden
};
RH_REGISTER_TYPE(PresentPushConstants)


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

private:
    // bloom chain and auto exposure between TAA and the tone mapping (PostProcessRenderer)
    void add_post_process_passes();

    // 1/2 .. 1/64 of the screen
    std::array<RGTextureHandle, PostProcessRenderer::bloom_levels> bloom_down;
    // 1/2 .. 1/32: bloom_up[0] is the bloom the tone mapping adds
    std::array<RGTextureHandle, PostProcessRenderer::bloom_levels - 1> bloom_up;
    // adapted exposure and the luminance histogram (resources/post_process.glsl)
    RGTextureHandle exposure;

    // the tone mapped frame (display encoded): the extensions of RenderStage::after_tonemap work on it (the JPEG
    // screen effects, jpeg_extension.cpp), pass Present copies it to the swapchain with the HUD (present.frag)
    RGTextureHandle ldr_color;
    RenderResource* present_resource = nullptr;
    std::shared_ptr<PipelineFamily> present_family;
    PipelineObject* present_pipeline = nullptr;

    // debug commands aimed with the camera (render.jpeg.hits.shoot, damage.look): the first static / dynamic body
    // through the middle of the screen (the player's capsule is a character: not hit)
    std::optional<phys::Hit> camera_center_hit() const;
    void run_camera_commands();

    
    
    //bool capture_ibl;
    
};
RH_OBJECT(GameRenderGraph)