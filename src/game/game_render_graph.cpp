module game:render_graph;

import render;
import vk;
import glm;
import rhmath;
import <vector>;
import profile;
import name;
import <unordered_map>;
import rhcomponents;
import globals;
import assets;
import <functional>;
import texture_format;
import :constants;
import :names;

#include "render_layout.h"
#include "common/assertion_macros.h"
#include "profiling/profile.h"


GameRenderGraph::GameRenderGraph()
{
    allow_shadow_debug = false;
    
    num_pass_instances = 1;
    capture_dimension = TextureDimension::Tex2D;
    resolution = Constants::zero_extent;
    use_swapchain_extent = true;
}

void GameRenderGraph::init_resources(const std::map<Name, bool>& init_params)
{
    set_flag(Names::debug_shadow2, false);
    GenericRenderGraph::init_resources(init_params);
}

void GameRenderGraph::build_passes(const std::map<Name, bool>& parameters)
{
    GenericRenderGraph::build_passes(parameters);
    
    
    add_pass({
            .name = "ToneMapping",
            .condition = [this] () { return !is_debugging(); },
            .reads = {
                { hdr_color, RBImageUsage::SampledFragment },
                { history_hdr, RBImageUsage::SampledFragment },
                
                
            },
            .writes = {
                { swapchain_color, RBImageUsage::ColorAttachment, RBLoadOp::Clear }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                if (ctx.bind_pipeline(tonemap_pipeline))
                {
                    ctx.bind(hdr_color_resource);
                }
                         
                ctx.backend.draw_fullscreen(ctx.cmd);
                
            },
    });
}

void GameRenderGraph::prepare_resources(RenderGraphContext& ctx)
{
    GenericRenderGraph::prepare_resources(ctx);
    
    auto hdr_color_instance = hdr_color_resource->query_single();
           
    hdr_color_instance->update_image(
        "u_hdr_color",
        get_image(hdr_color),
        {
            .frame = ctx.frame
        }
    );

    uint32_t prev_layer = history_index ^ 1;

    hdr_color_instance->update_image(
        "u_history",
        get_image(history_hdr),
        {
            .frame = ctx.frame,
            .layer_index = prev_layer
        }
    );
    
    
    
    
    auto shadow_debug_instance = shadow_resource->query_single();
    
    shadow_debug_instance->update_image(
        "u_shadow_depth",
        get_image(shadow_map),
        {
            .frame = ctx.frame
        }
    );
}

void GameRenderGraph::pass_shadow_map(RenderGraphContext& ctx)
{
    draw_scene_shadow(ctx);
}

void GameRenderGraph::pass_shadow_debug(RenderGraphContext& ctx)
{
    if (ctx.bind_pipeline(shadow_debug_pipeline))
    {
        ctx.bind(shadow_resource);
    }
    
    ctx.backend.draw_fullscreen(ctx.cmd);
}

void GameRenderGraph::pass_translucent(RenderGraphContext& ctx)
{
    PROFILE("Translucent");
            
    draw_scene(ctx);
}

void GameRenderGraph::pass_clouds(RenderGraphContext& ctx)
{
    draw_clouds(ctx, g_depth, noise_texture);
}

void GameRenderGraph::pass_tonemapping(RenderGraphContext& ctx)
{
            
   
}




void GameRenderGraph::pass_readback(RenderGraphContext& ctx)
{
    
}