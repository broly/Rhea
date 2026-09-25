module;

#include <json/value.h>

export module game:cubemap_capture_render_graph;

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

class CubemapCaptureRenderGraph : public GenericRenderGraph
{
public:
    CubemapCaptureRenderGraph();
    
    void init_resources(const std::map<Name, bool>& parameters) override;
    void build_passes(const std::map<Name, bool>& parameters) override;
    void prepare_resources(RenderGraphContext& ctx) override;
    
    CameraUBO make_camera_ubo(RenderGraphContext& ctx, bool zero_pos, uint32_t face_index) const override;

    RGTextureHandle irradiance;
    RGTextureHandle prefiltered_env;
    RGTextureHandle brdf_lut;
    
    PipelineObject* irradiance_pipeline;
    PipelineObject* prefilter_pipeline;
    PipelineObject* brdf_lut_pipeline;
    
    // std::shared_ptr<Material> irradiance_material;
    // std::shared_ptr<Material> prefilter_material;
    // std::shared_ptr<Material> brdf_lut_material;
    
    // std::shared_ptr<MaterialInstance> irradiance_material_instance;
    
    std::shared_ptr<PipelineFamily> irradiance_pipeline_family;
    std::shared_ptr<PipelineFamily> prefilter_pipeline_family;
    std::shared_ptr<PipelineFamily> brdf_lut_pipeline_family;

};
RH_OBJECT(CubemapCaptureRenderGraph)