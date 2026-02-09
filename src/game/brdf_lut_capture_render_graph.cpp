module game:brdf_lut_capture_render_graph;
import texture_format;
import :constants;
import :names;
#include "common/assertion_macros.h"


BrdfLutCaptureRenderGraph::BrdfLutCaptureRenderGraph()
{

}

void BrdfLutCaptureRenderGraph::init_resources(const std::map<Name, bool>& parameters)
{
    
    auto brdf_lut_model = renderer->find_model("IBL_BRDF_LUT");
    PipelineFamily brdf_lut_pipeline_family("IBL_BRDF_LUT", brdf_lut_model, backend, renderer);
    brdf_lut_pipeline = request_pipeline(brdf_lut_pipeline_family, {});
    
    
    // === BRDF LUT ===
    RGTextureDesc brdf_lut_desc{
        .name = "brdf_lut",
        .extent = {512, 512},
        .format = TextureFormat::RGBA16F,  //todo
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
        .external = false,
        .dimension = TextureDimension::Tex2D
    };
    brdf_lut = create_texture(brdf_lut_desc);
    
}



void BrdfLutCaptureRenderGraph::prepare_resources(RenderGraphContext& ctx)
{
    
           
    if (!brdf_lut_material)
    {
        brdf_lut_material = std::make_shared<Material>();
        brdf_lut_material->model = "IBL_BRDF_LUT";
    }

    {
        auto mat_inst = renderer->query_material_instance(brdf_lut_material, "IBL_BRDF_LUT");
        auto* inst = mat_inst->get_or_create_resource_instance(brdf_lut_pipeline, ctx.frame);
    }
}

void BrdfLutCaptureRenderGraph::build_passes(const std::map<Name, bool>& parameters)
{
    

   // Pass: compute BRDF LUT
   add_pass({
       .name = "IBL_BRDF_LUT",
       .writes = { { brdf_lut, RBImageUsage::ColorAttachment, RBLoadOp::Clear } },
       .execute = [this](RenderGraphContext& ctx) {
           ctx.backend.bind_pipeline(ctx.cmd, brdf_lut_pipeline);
   
           auto mat_inst = renderer->query_material_instance(brdf_lut_material, ctx.pass_name);
           auto* inst = mat_inst->get_or_create_resource_instance(brdf_lut_pipeline, ctx.frame);
   
           inst->bind(brdf_lut_pipeline, ctx.cmd, ctx.frame);
           ctx.backend.draw_fullscreen(ctx.cmd, RBDrawParams{
               .update_viewport_extent = true,
               .extent = {512, 512}
           });
       }
   });
}
