export module game:cubemap_capture_render_graph;


import render;
import glm;
import name;
import <map>;
import <memory>;
import engine;
import assets;
import :generic_render_graph;
#include "object/object_reflection_macro.h"

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
    
    std::shared_ptr<Material> irradiance_material;
    std::shared_ptr<Material> prefilter_material;
    std::shared_ptr<Material> brdf_lut_material;
    
    std::shared_ptr<MaterialInstance> irradiance_material_instance;

};
REFLECT_OBJECT(CubemapCaptureRenderGraph, GenericRenderGraph)