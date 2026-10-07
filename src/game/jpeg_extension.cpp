module game;

import :jpeg;
import :screen_jpeg;
import :jpeg_hits;

import std.compat;
import assertions;

import render;
import glm;
import name;
import rhmath;
import profile;
import texture_format;
import engine;
import globals;

#include "common/assertion_macros.h"
#include "profiling/profile.h"

namespace
{
    // The shakalizer over the tone mapped frame (RenderStage::after_tonemap), the first user of the render graph
    // extensions. ldr_color goes through the chains:
    //   ScreenJpeg             the full screen JPEG damage (screen_jpeg): only while the player is hurt or hit
    //   JpegHitMask, HitJpeg*  the JPEG spots of the hits (jpeg_hits): only while a hit lives, a chain per slot
    //                          (the preset of a weapon), masked by the strength of the hits per 8 x 8 block
    //   JpegComposite          ldr_color with their results (and the red edges of the damage) -> jpeg_composite
    //   COPY_jpeg_to_ldr       back to ldr_color, which Present shows
    // Nothing runs while no damage, hit or red edge is on.
    class JpegScreenEffects final : public RenderGraphExtension
    {
    public:
        JpegScreenEffects()
            : RenderGraphExtension("JpegScreenEffects", RenderStage::after_tonemap)
        {
        }

        void build(RenderGraph& graph, const std::function<bool()>& active) override
        {
            renderer.init(*graph.renderer, *graph.backend);
            ldr_color = graph.texture("ldr_color");

            // divisor 1: the screen size, kept through resizes (the shakalizer reads the extent)
            screen_jpeg_color = graph.create_texture({
                .name = "screen_jpeg",
                .extent_divisor = 1,
                .format = TextureFormat::RGBA8_UNORM,
                .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
            });
            // one texel per 8 x 8 pixels (JPEG_MASK_BLOCK), a channel per hit slot
            hit_mask = graph.create_texture({
                .name = "jpeg_hit_mask",
                .extent_divisor = 8,
                .format = TextureFormat::RGBA8_UNORM,
                .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
            });
            hit_jpeg_work = graph.create_texture({
                .name = "hit_jpeg_work",
                .extent_divisor = 1,
                .format = TextureFormat::RGBA8_UNORM,
                .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
            });
            hit_jpeg_color = graph.create_texture({
                .name = "hit_jpeg",
                .extent_divisor = 1,
                .format = TextureFormat::RGBA8_UNORM,
                .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled,
            });
            composite = graph.create_texture({
                .name = "jpeg_composite",
                .extent_divisor = 1,
                .format = TextureFormat::RGBA8_UNORM,
                .usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
            });

            renderer.add_chain(graph, {
                .name = "ScreenJpeg",
                .source = ldr_color,
                .target = screen_jpeg_color,
                .condition = [this, active] () { return active() && screen_state.active; },
                .settings = [this] () { return screen_state.settings; },
            });

            Renderer& render = *graph.renderer;
            renderer.add_hit_mask_pass(graph, hit_mask, graph.texture("g_linear_depth"), graph.texture("g_geometry_normal"),
                render.find_resource("camera"), render.find_resource("gbuffer"), [this, active] () { return active() && hits.any; });
            for (uint32_t slot = 0; slot < jpeg_hits::slot_count; ++slot)
            {
                renderer.add_chain(graph, {
                    .name = Name(std::format("HitJpeg{}", slot)),
                    .source = ldr_color,
                    .target = hit_jpeg_color,
                    .condition = [this, active, slot] () { return active() && hits.slot_active[slot]; },
                    .settings = [this, slot] () { return hits.slot_settings[slot]; },
                    .mask = hit_mask,
                    .mask_channel = slot,
                    .work = hit_jpeg_work,
                });
            }

            const std::function<bool()> composite_needed = [this, active] () { return active() && composite_on(); };
            graph.add_pass({
                .name = "JpegComposite",
                .condition = composite_needed,
                .reads = {
                    { ldr_color, RBImageUsageType::Sampled },
                    { screen_jpeg_color, RBImageUsageType::Sampled },
                    { hit_jpeg_color, RBImageUsageType::Sampled },
                    { hit_mask, RBImageUsageType::Sampled },
                },
                .writes = {
                    { composite, RBImageUsageType::StorageImage },
                },
                .execute = [this] (RenderGraphContext& ctx)
                {
                    renderer.dispatch_composite(ctx, composite_params(), ctx.render_graph.textures[composite.id].desc.extent);
                },
                .type = RenderPassType::compute
            });
            graph.add_pass({
                .name = "COPY_jpeg_to_ldr",
                .condition = composite_needed,
                .reads = {
                    { composite, RBImageUsageType::TransferSrc },
                },
                .writes = {
                    { ldr_color, RBImageUsageType::TransferDst, RBLoadOp::Load },
                },
                .execute = [this] (RenderGraphContext& ctx)
                {
                    CopyImageParams params;
                    params.source = ctx.render_graph.get_image(composite);
                    params.dest = ctx.render_graph.get_image(ldr_color);
                    ctx.copy_img(params);
                },
                .type = RenderPassType::transfer
            });
        }

        void prepare(RenderGraphContext& ctx, RenderGraph& graph) override
        {
            PROFILE("JpegScreenEffects::prepare");
            const float delta = (float)RhGlobals::engine->world->get_delta_seconds();
            screen_jpeg::tick(delta);
            screen_state = screen_jpeg::evaluate();
            jpeg_hits::tick(delta);
            hits = jpeg_hits::update();

            renderer.prepare(ctx, graph);
            renderer.prepare_hit_mask(ctx, graph.get_image(hit_mask), hits.ubo);
            renderer.prepare_composite(ctx, graph.get_image(ldr_color), graph.get_image(screen_jpeg_color),
                graph.get_image(hit_jpeg_color), graph.get_image(hit_mask), graph.get_image(composite));
        }

    private:
        // the composite changes the frame: damage (its share or vignette), hits or red edges
        bool composite_on() const
        {
            const glm::vec3& tint = screen_state.tint;
            return screen_state.active || hits.any || std::max({ tint.r, tint.g, tint.b }) > 0.0f;
        }

        JpegCompositeParams composite_params() const
        {
            const screen_jpeg::State& damage = screen_state;
            return {
                .amount = damage.active ? damage.amount : 0.0f,
                .vignette_radius = damage.active ? damage.vignette_radius : 10.0f,
                .vignette_feather = damage.vignette_feather,
                .block = damage.block,
                .seed = damage.seed,
                .tint = damage.tint,
                .tint_radius = damage.tint_radius,
                .hits = hits.any,
            };
        }

        JpegRenderer renderer;
        RGTextureHandle ldr_color{};
        RGTextureHandle screen_jpeg_color{};
        RGTextureHandle hit_mask{};
        RGTextureHandle hit_jpeg_work{};
        RGTextureHandle hit_jpeg_color{};
        RGTextureHandle composite{};
        screen_jpeg::State screen_state;
        jpeg_hits::Frame hits;
    };

    JpegScreenEffects jpeg_screen_effects;
}
