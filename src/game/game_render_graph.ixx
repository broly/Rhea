export module game:render_graph;

import render;
import glm;
import name;
import <map>;
import <memory>;
import engine;
import assets;
import :generic_render_graph;
#include "object/object_reflection_macro.h"

struct TonemapPushConstants
{
    float time;
};
REFLECT_STRUCT_RUNTIME(TonemapPushConstants, 
    time);


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
    void pass_clouds(RenderGraphContext& ctx);
    void pass_tonemapping(RenderGraphContext& ctx);
    void pass_readback(RenderGraphContext& ctx);

    
    
    //bool capture_ibl;
    
};
REFLECT_OBJECT(GameRenderGraph, GenericRenderGraph);