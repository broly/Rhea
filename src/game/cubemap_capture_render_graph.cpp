module game:cubemap_capture_render_graph;
import texture_format;
import :constants;
import :names;
#include "common/assertion_macros.h"

constexpr unsigned floorlog2(unsigned x)
{
    return x == 1 ? 0 : 1+floorlog2(x >> 1);
}

constexpr uint32_t CUBEMAP_PREFILTER_FACE_SIZE = 128;
constexpr uint32_t CUBEMAP_PREFILTER_FACE_NUM_MIPS = floorlog2(CUBEMAP_PREFILTER_FACE_SIZE);


CubemapCaptureRenderGraph::CubemapCaptureRenderGraph()
{
    allow_shadow_debug = false;
    
    num_pass_instances = 6;
    capture_dimension = TextureDimension::Cube;
    resolution = Constants::ibl_extent;
    use_swapchain_extent = false;
}

void CubemapCaptureRenderGraph::init_resources(const std::map<Name, bool>& parameters)
{
    GenericRenderGraph::init_resources(parameters);
    
    auto irradiance_model = renderer->find_model("IBL_Irradiance");
    PipelineFamily irradiance_pipeline_family("IBL_Irradiance", irradiance_model, backend, renderer);
    irradiance_pipeline = request_pipeline(irradiance_pipeline_family, {});
    
    auto prefilter_model = renderer->find_model("IBL_Prefilter");
    PipelineFamily prefilter_pipeline_family("IBL_Prefilter", prefilter_model, backend, renderer);
    prefilter_pipeline = request_pipeline(prefilter_pipeline_family, {});
    
    auto brdf_lut_model = renderer->find_model("IBL_BRDF_LUT");
    PipelineFamily brdf_lut_pipeline_family("IBL_BRDF_LUT", brdf_lut_model, backend, renderer);
    brdf_lut_pipeline = request_pipeline(brdf_lut_pipeline_family, {});
    
    RGTextureDesc irradiance_color_desc{
        .name = "irradiance",
        .extent = resolution,
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
        .external = false,
        .dimension = capture_dimension
    };
    
    irradiance = create_texture(irradiance_color_desc);
    
    // === Specular pre-filtered environment map ===
    RGTextureDesc prefiltered_color_desc{
        .name = "prefiltered_env",
        .extent = {CUBEMAP_PREFILTER_FACE_SIZE, CUBEMAP_PREFILTER_FACE_SIZE},
        .format = TextureFormat::RGBA16F,
        .usage  = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::TransferSrc,
        .external = false,
        .dimension = capture_dimension,
        .mip_levels = CUBEMAP_PREFILTER_FACE_NUM_MIPS
        
    };
    prefiltered_env = create_texture(prefiltered_color_desc);
    
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



void CubemapCaptureRenderGraph::prepare_resources(RenderGraphContext& ctx)
{
    GenericRenderGraph::prepare_resources(ctx);
    
    
    if (!irradiance_material)
    {
        irradiance_material = std::make_shared<Material>();
        irradiance_material->model = "IBL_Irradiance";
    }
    
    
    if (!prefilter_material)
    {
        prefilter_material = std::make_shared<Material>();
        prefilter_material->model = "IBL_Prefilter";
    }
    
    
           
    if (!brdf_lut_material)
    {
        brdf_lut_material = std::make_shared<Material>();
        brdf_lut_material->model = "IBL_BRDF_LUT";
    }

    
    {
        auto mat_inst = renderer->query_material_instance(irradiance_material, "IBL_Irradiance");
        auto* inst = mat_inst->get_or_create_resource_instance(irradiance_pipeline, ctx.frame);
    
        inst->update_image(irradiance_pipeline, "u_env_map", get_image(hdr_color), ctx.frame, 0, true);
    }
    {
        auto mat_inst = renderer->query_material_instance(prefilter_material, "IBL_Prefilter");
        auto* inst = mat_inst->get_or_create_resource_instance(prefilter_pipeline, ctx.frame);

        inst->update_image(prefilter_pipeline, "u_env_map", get_image(hdr_color), ctx.frame, 0, true);
    } 
    
    {
        
   
        auto mat_inst = renderer->query_material_instance(brdf_lut_material, "IBL_BRDF_LUT");
        auto* inst = mat_inst->get_or_create_resource_instance(brdf_lut_pipeline, ctx.frame);
   
    }
}

void CubemapCaptureRenderGraph::build_passes(const std::map<Name, bool>& parameters)
{
    GenericRenderGraph::build_passes(parameters);
    
    
   add_pass({
       .name = "IBL_Irradiance",
       .reads = { { hdr_color, RBImageUsage::SampledFragment } },
       .writes = { { irradiance, RBImageUsage::ColorAttachment, RBLoadOp::Clear } },
       .execute = [this](RenderGraphContext& ctx) {
           ctx.backend.bind_pipeline(ctx.cmd, irradiance_pipeline);
           
   
           auto mat_inst = renderer->query_material_instance(irradiance_material, ctx.pass_name);
           auto* inst = mat_inst->get_or_create_resource_instance(irradiance_pipeline, ctx.frame);
   
           // inst->update_image(irradiance_pipeline, "u_env_map", get_image(hdr_color), ctx.frame, 0, true);
           inst->bind(irradiance_pipeline, ctx.cmd, ctx.frame);
   
           const uint32_t pc = ctx.level;
           ctx.backend.push_constants(ctx.cmd, pc, irradiance_pipeline);
           ctx.backend.draw_fullscreen(ctx.cmd, RBDrawParams {  
               .update_viewport_extent = true,
               .extent = Constants::ibl_extent
           });
       },
       .num_layers = 6,
   });
   
   add_pass({
       .name = "IBL_Prefilter",
       .reads = { { hdr_color, RBImageUsage::SampledFragment } },
       .writes = { { prefiltered_env, RBImageUsage::ColorAttachment, RBLoadOp::Clear } },
       .execute = [this](RenderGraphContext& ctx) {
           ctx.backend.bind_pipeline(ctx.cmd, prefilter_pipeline);
           
           auto mat_inst = renderer->query_material_instance(prefilter_material, ctx.pass_name);
           auto* inst = mat_inst->get_or_create_resource_instance(prefilter_pipeline, ctx.frame);
   
           inst->bind(prefilter_pipeline, ctx.cmd, ctx.frame);
   
           LevelAndMip pc;
           pc.level = ctx.level;
           pc.roghness = (float)ctx.mip / (float)(CUBEMAP_PREFILTER_FACE_NUM_MIPS - 1);
           ctx.backend.push_constants(ctx.cmd, pc, prefilter_pipeline);
           ctx.backend.draw_fullscreen(ctx.cmd, RBDrawParams {  
               .update_viewport_extent = true,
               .extent = Constants::ibl_prefilter_extent >> ctx.mip
           });
       },
       .num_layers = 6,
       .num_mip_maps = CUBEMAP_PREFILTER_FACE_NUM_MIPS
   });

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
   add_pass({
       .name = "readback",
       .condition = [this] () { return !is_debugging(); },
       .reads = {
           { hdr_color,  RBImageUsage::TransferSrc }
       },
       .execute = [this] (RenderGraphContext& ctx)
       {
           
       },
   });
}

CameraUBO CubemapCaptureRenderGraph::make_camera_ubo(RenderGraphContext& ctx, bool zero_pos, uint32_t face_index) const
{
    auto& scene_view = engine->scene_view;
    CameraUBO camera_ubo;
    vec3 camera_position;
    
    auto pos_it = ctx.params.vec3_params.find("capture_pos");
    checkf(pos_it != ctx.params.vec3_params.end(), "position not found");

        
    auto position = pos_it->second;
        
    static const glm::vec3 cube_dirs[6] = {
        { 1,  0,  0}, {-1,  0,  0},
        { 0,  1,  0}, { 0, -1,  0},
        { 0,  0,  1}, { 0,  0, -1}
    };

    static const glm::vec3 cube_ups[6] = {
        {0, -1,  0}, {0, -1,  0},
        {0,  0,  1}, {0,  0, -1},
        {0, -1,  0}, {0, -1,  0}
    };
        

    glm::mat4 view = glm::lookAt(
        position,
        position + cube_dirs[face_index],
        cube_ups[face_index]
    );

    glm::mat4 proj = glm::perspective(
        glm::radians(90.f),
        1.f,
        0.1f,
        1000.f
    );

    proj[1][1] *= -1; // vk ndc

    camera_ubo.proj = proj;
    camera_ubo.view = view;
    camera_ubo.camera_pos = position;
    camera_position = position;
    
    return camera_ubo;
}
