module;

#include <json/value.h>

export module game:brdf_lut_capture_render_graph;

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
#include "object/object_reflection_macro.h"

struct LevelAndMip
{
    uint32_t level;
    float roghness;
};
RH_REGISTER_TYPE(LevelAndMip)

class BrdfLutCaptureRenderGraph : public RenderGraph
{
public:
    BrdfLutCaptureRenderGraph();
    
    void init_resources(const std::map<Name, bool>& parameters) override;
    void build_passes(const std::map<Name, bool>& parameters) override;
    void prepare_resources(RenderGraphContext& ctx) override;

    RGTextureHandle brdf_lut;
    PipelineObject* brdf_lut_pipeline;
    //std::shared_ptr<Material> brdf_lut_material;
    
    std::shared_ptr<PipelineFamily> brdf_lut_pipeline_family;

};
RH_OBJECT(BrdfLutCaptureRenderGraph)