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
                { normal_color, RBImageUsage::SampledFragment },
            },
            .writes = {
                { swapchain_color, RBImageUsage::ColorAttachment, RBLoadOp::Clear }
            },
            .execute = [this] (RenderGraphContext& ctx)
            {
                ctx.backend.bind_pipeline(ctx.cmd, tonemap_pipeline);
                
                std::shared_ptr<MaterialInstance> material_instance =
                    renderer->query_material_instance(tonemap_material, ctx.pass_name);
                 
                auto tonemap_instance =
                    material_instance->get_or_create_resource_instance(
                        ctx.frame
                    );
                         
                tonemap_instance->update_image(
                    "u_hdr_color",
                    get_image(normal_color),
                    ctx.frame
                );
                         
                tonemap_instance->bind(
                    ctx.cmd,
                    ctx.frame
                );
                         
                ctx.backend.draw_fullscreen(ctx.cmd);
                
            },
    });
}

void GameRenderGraph::pass_shadow_map(RenderGraphContext& ctx)
{
    draw_scene_shadow(ctx);
}

void GameRenderGraph::pass_shadow_debug(RenderGraphContext& ctx)
{
    
    ctx.backend.bind_pipeline(ctx.cmd, shadow_debug_pipeline);
            
    auto shadow_debug_material_instance = renderer->query_material_instance(shadow_debug_material, ctx.pass_name);
    
    auto shadow_debug_instance =
        shadow_debug_material_instance->get_or_create_resource_instance(
            ctx.frame
        );
    
    shadow_debug_instance->update_image(
        "u_shadow_depth",
        get_image(shadow_map),
        ctx.frame
    );
        
    shadow_debug_instance->bind(
        ctx.cmd,
        ctx.frame
    );
    
    ctx.backend.draw_fullscreen(ctx.cmd);
}

void GameRenderGraph::pass_translucent(RenderGraphContext& ctx)
{
    PROFILE("Translucent");
            
    draw_scene(ctx);
}

void GameRenderGraph::pass_clouds(RenderGraphContext& ctx)
{
    draw_clouds(ctx, depth_texture, noise_texture);
}

void GameRenderGraph::pass_tonemapping(RenderGraphContext& ctx)
{
            
   
}




void GameRenderGraph::pass_readback(RenderGraphContext& ctx)
{
    
}