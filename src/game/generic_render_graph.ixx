module;

#include <json/value.h>

export module game:generic_render_graph;

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
import rhmath;
import debug_draw;
import :nn_denoiser_passes;
import :debug_view;
import :reflection_probes;
import :sky_renderer;
import :particle_renderer;
import :draw_list;
import rhcomponents;
import cvar;
#include "object/object_reflection_macro.h"

// Time sliced shadow cascades (GenericRenderGraph::update_shadow_cascades): cascades 0 and 1 render every frame,
// the far ones (most of the terrain) every few frames
export cvar::Var<int> cv_shadow_interval_2(
    "render.shadows.interval_2", 2, "Frames between renders of shadow cascade 2 (1: every frame)");
export cvar::Var<int> cv_shadow_interval_3(
    "render.shadows.interval_3", 4, "Frames between renders of shadow cascade 3 (1: every frame)");

// A shadow cascade as rendered into its atlas tile: the light UBO carries these, so a tile that was not rendered
// this frame is still sampled with the matrix it was rendered with
export struct ShadowCascade
{
    glm::mat4 vp{ 1.0f };
    glm::vec3 center{ 0.0f };           // world, of the fitted sphere
    float radius = 0.0f;
    float texel = 0.0f;                 // world size of a shadow texel
    float depth_range = 1.0f;           // m between the near and far plane
    glm::vec3 light_direction{ 0.0f };
    bool valid = false;
    bool rendered_this_frame = false;
};


struct DrawItem
{
    MeshPrimHandle mesh;
    glm::mat4 world;
    std::shared_ptr<Material> material;
};

struct DrawBatch
{
    PipelineObject* pipeline = nullptr;
    std::vector<DrawItem> items;
};

struct ViewInfo
{
    glm::mat4 view;
    glm::mat4 proj;
    Frustum frustum;
};

enum class COLOR_OUTPUT_HDR : uint8_t
{
    BASE = 0,
    RTXGI = 1,
    SSR = 2,
    INTERMEDIATE = 3,
    SPECULAR_WEIGHT = 4,     // lighting -> SSRComposite: env BRDF x specular occlusion
    RTXGI_REPROJECTED = 5,
    RTXGI_ACCUM = 6,
    RTXGI_MOMENTS = 7,
    RTXGI_FILTERED = 8,
    RTXGI_NEURAL_DENOISED = 9,
    RESERVED_2 = 10,
    
};


enum class GBUFFER_SLOTS : uint8_t
{
    NORMAL = 0,
    WORLD_NORMAL = 1,
    DEPTH = 2,  // special
    LINEAR_DEPTH = 3,
    ALBEDO_ROUGHNESS = 4,
    POSITION = 5,
    MOTION_VECTORS = 6,
    GEOMETRY_NORMAL = 7,
    EMISSIVE = 8,
};


struct HDROutputTextureArray : std::vector<RGTextureHandle>
{
    using std::vector<RGTextureHandle>::vector;
    HDROutputTextureArray() {}
    HDROutputTextureArray(const std::vector<RGTextureHandle>& array)
        : std::vector<RGTextureHandle>(array)
    {}
    using std::vector<RGTextureHandle>::operator[];
    RGTextureHandle operator[](COLOR_OUTPUT_HDR enumerator)
    {
        return std::vector<RGTextureHandle>::operator[](static_cast<size_t>(enumerator));
    }
};

struct GBufferArray : std::vector<RGTextureHandle>
{
    using std::vector<RGTextureHandle>::vector;
    GBufferArray() {};
    GBufferArray(const std::vector<RGTextureHandle>& array)
        : std::vector<RGTextureHandle>(array)
    {}
    using std::vector<RGTextureHandle>::operator[];
    RGTextureHandle operator[](GBUFFER_SLOTS enumerator)
    {
        return std::vector<RGTextureHandle>::operator[](static_cast<size_t>(enumerator));
    }
};

// lighting.frag: take indirect light from the reflection capture IBL instead of the RTXGI output
constexpr uint32_t LIGHTING_GI_FROM_IBL = 0xFFFFFFFFu;

// Renderer int param "lighting_debug" (bit mask, lighting.frag): drops one lighting term to bisect artifacts
namespace LightingDebug
{
    inline constexpr const char* param = "lighting_debug";
    inline constexpr uint32_t no_diffuse_gi = 1;    // irradiance / GI buffer
    inline constexpr uint32_t no_specular_ibl = 2;  // image based specular: reflection probes and SSR (specular weight 0)
    inline constexpr uint32_t no_emissive = 4;      // g-buffer emissive (and character default lit emissive)
    inline constexpr uint32_t no_shadows = 8;       // directional shadow map
    inline constexpr uint32_t no_decals = 16;       // decal albedo
    // counts legacy-model pixels on the screen periphery (the level: no emissive textures) with a non-zero
    // g-buffer emissive (discarded, so they show black), logs frames where they appear and auto-dumps
    // g-buffer EXRs (see read_diag_queries)
    inline constexpr uint32_t emissive_watch = 32;
    // every surface is a mirror of the reflection probes (roughness 0, parallax corrected): shows what the
    // probes captured and how their boxes line up with the geometry
    inline constexpr uint32_t probe_mirror = 64;
}

// Renderer int param "geometry_debug" (bit mask, ModelPushConstants.debug_id of the base pass draws)
namespace GeometryDebug
{
    inline constexpr const char* param = "geometry_debug";
    inline constexpr uint32_t zero_emissive = 1;         // g-buffer emissive written as 0 (pbr and character)
    inline constexpr uint32_t emissive_index_check = 2;  // pbr: emissive = red where the material has an emissive texture
    inline constexpr uint32_t solid_emissive = 4;        // emissive target = solid red (checks the switch reaches the shaders)
    inline constexpr uint32_t glossy = 8;                // pbr roughness x 0.25: polished level to check SSR / probe reflections
}

struct ColorOutputConstants
{
    uint32_t buffer_index;
    uint32_t debug_flags = 0;  // LightingDebug bits
};
RH_REGISTER_TYPE(ColorOutputConstants)

class GenericRenderGraph : public RenderGraph
{
public:
    GenericRenderGraph();
    
    void init_resources(const std::map<Name, bool>& parameters) override;
    void build_passes(const std::map<Name, bool>& parameters) override;
    void prepare_resources(RenderGraphContext& ctx) override;
    void prepare_raytracing(RenderGraphContext& ctx);
    void begin_frame() override;
    void end_frame() override;
    
    void rebuild_camera_ubo(RenderGraphContext& ctx);
    
    virtual void on_pso_built() override;

    void prepare_geometry_resources(RenderGraphContext& ctx);
    void prepare_shadow_pass(RenderGraphContext& ctx);
    void prepare_wireframe_pass(RenderGraphContext& ctx);
    void prepare_ssr(RenderGraphContext& ctx);
    
    void setup_hdr_color_table(RenderGraphContext& ctx) const;


    void dispatch_skinning(RenderGraphContext& ctx);
    void draw_scene(RenderGraphContext& ctx);
    void draw_scene_shadow(RenderGraphContext& ctx);
    void draw_wireframe(RenderGraphContext& ctx);
    void draw_ssr(RenderGraphContext& ctx);
    void draw_ssr_composite(RenderGraphContext& ctx);
    void draw_rtxgi(RenderGraphContext& ctx);
    
    // Debug views drawn over the tonemapped image: mesh wireframe (DebugViewMode::WIREFRAME*),
    // skeletons of skinned meshes and debug_draw lines. Must be added after the pass that writes
    // the swapchain.
    void add_debug_overlay_pass();
    void draw_debug_overlay(RenderGraphContext& ctx);
    void draw_mesh_wireframe(RenderGraphContext& ctx);
    void add_skeleton_lines(std::vector<LineVertex>& vertices);
    void draw_debug_lines(RenderGraphContext& ctx);
    
    // condition: the pass runs on the frames it returns true for (every frame when empty)
    void add_copy_pass(Name name, RGTextureHandle src, RGTextureHandle dst, bool ping_pong = true,
        std::function<bool()> condition = {});
    
    void prepare_nn_denoiser_passes();

    ViewInfo  build_view_info(
        RenderGraphContext& ctx,
        bool zero_pos) const;
    
    bool is_debugging() const;
    
    // cascades of the directional light's shadow fitted around current_camera_ubo, casters from the scene bounds
    std::array<ShadowCascade, shadow_cascade_count> fit_shadow_cascades(const glm::vec3& light_direction) const;
    // picks the cascades to render this frame (shadow_cascades): 0 and 1 always, the far ones at their interval,
    // any of them when its fit moved or the sun turned too far, all when the atlas is new
    void update_shadow_cascades(const glm::vec3& light_direction);
    // the rendered cascades -> the light UBO
    void write_shadow_cascades(DirectionalLight& light) const;

    const std::array<ShadowCascade, shadow_cascade_count>& get_shadow_cascades() const { return shadow_cascades; }
    
    virtual CameraUBO make_camera_ubo(RenderGraphContext& ctx, bool zero_pos = false, uint32_t face_index = 0) const;
    LightUBO build_light_ubo(glm::vec3 camera_position) const;
    void bind_shadow_globals(
        RenderGraphContext& ctx);
    
    RGTextureHandle shadow_map;
    // what the clouds let through of the directional light, a map across it around the camera (SkyRenderer)
    RGTextureHandle cloud_shadow_map;
    // sky of the frame at 1 / sky_downscale of the screen (SkyRenderer): atmosphere, clouds, and the clouds
    // of the two previous frames (layer history_index is written, the other one read)
    RGTextureHandle atmosphere_buffer;
    RGTextureHandle clouds_buffer;
    RGTextureHandle clouds_history;
    uint32_t sky_downscale = 1;     // render.sky.downscale the buffers were created with
    // fog of the frame at 1 / fog_downscale of the screen (SkyRenderer), and of the two previous frames
    RGTextureHandle fog_buffer;
    RGTextureHandle fog_history;
    uint32_t fog_downscale = 1;     // render.fog.downscale the buffers were created with
    RGTextureHandle swapchain_color;
    
    HDROutputTextureArray hdr_color_present;
    HDROutputTextureArray hdr_color_history;
    
    GBufferArray gbuffer;
    GBufferArray gbuffer_hist;
    RGTextureHandle decal_albedo;
    
    RGTextureHandle ssr_texture;
    uint32_t history_index = 0;

    // Full-screen "sky flash" diagnostics: occlusion queries per frame in flight, [frame * DIAG_COUNT + DIAG_*]:
    // samples drawn by the sky pass (where depth is empty) and by the base geometry pass.
    // Read back when the frame slot comes around again, logged when the sky covers most of the screen.
    // DIAG_LIGHTING: samples the lighting pass kept with LightingDebug::emissive_watch.
    static constexpr uint32_t DIAG_CLOUDS = 0;
    static constexpr uint32_t DIAG_GEOMETRY = 1;
    static constexpr uint32_t DIAG_LIGHTING = 2;
    static constexpr uint32_t DIAG_COUNT = 3;
    RBQueryPool diag_queries;
    uint64_t diag_frame_counter = 0;
    uint32_t emissive_watch_dumps = 0;
    double emissive_watch_last_dump_time = -1.0e9;
    double emissive_watch_last_log_time = -1.0e9;
    uint32_t emissive_watch_frames_since_log = 0;
    // totals, logged every 30 s while the watch is on
    uint64_t emissive_watch_frames = 0;
    uint64_t emissive_watch_flagged = 0;
    uint64_t emissive_watch_bursts = 0;
    bool emissive_watch_prev_flagged = false;
    double emissive_watch_last_summary_time = -1.0;
    // Emissive watch ring: g_emissive copied into one layer after the base pass every frame while the watch is on.
    // A corrupted frame is only reported MAX_FRAMES_IN_FLIGHT frames later (and often lasts one frame), its copy
    // is still intact then and the auto-dump saves every ring layer (the log names the corrupted frame's layer)
    static constexpr uint32_t EMISSIVE_RING_SIZE = kRenderMaxFramesInFlight + 1;
    RGTextureHandle emissive_ring;
    uint32_t emissive_ring_next = 0;
    std::array<uint32_t, kRenderMaxFramesInFlight> emissive_ring_layer_of_slot = {};
    bool emissive_watch_active = false;
    uint32_t renderdoc_captures_requested = 0;
    double renderdoc_last_request_time = -1.0e9;
    bool diag_enabled() const { return num_pass_instances == 1 && diag_queries.handle != 0; }
    void read_diag_queries(RenderGraphContext& ctx);
    // Diagnostics of the blocky g-buffer emissive garbage (GPU load-step transient, see the emissive watch):
    // occlusion queries (sky flash, base pass, emissive watch), the watch ring and its F9 dump entries.
    // Off by default; RHEA_EMISSIVE_WATCH=1 turns them on (implied by RHEA_SOAK_SECONDS soak runs)
    static bool emissive_diagnostics_enabled();

    RGTextureHandle brdf_lut;
    
    RBAccelStruct tlas = {};
    
    [[=rh::resource]]
    RenderResource* camera_resource;
    
    [[=rh::resource]]
    RenderResource* gbuffer_resource;
    
    [[=rh::resource]]
    RenderResource* dbuffer_resource;
    
    [[=rh::resource]]
    RenderResource* light_resource;
    
    [[=rh::resource]]
    RenderResource* shadow_resource;
    
    [[=rh::resource]]
    RenderResource* reflection_resource;
    
    [[=rh::resource]]
    RenderResource* ssr_resource;
    
    [[=rh::resource]]
    RenderResource* hdr_color_output_resource;
    
    [[=rh::resource]]
    RenderResource* hdr_color_storage_resource;
    
    [[=rh::resource]]
    RenderResource* tlas_resource;
    
    [[=rh::resource]]
    RenderResource* mesh_table_resource;
    
    [[=rh::resource]]
    RenderResource* base_color_resource;
    
    [[=rh::resource]]
    RenderResource* pbr_material_table_resource;
    
    [[=rh::resource]]
    RenderResource* textures_resource;
    
    [[=rh::resource]]
    RenderResource* primitive_table_resource;
    
    Extent resolution;
    Extent swapchain_extent;
    
    TextureDimension capture_dimension;
    uint32_t num_pass_instances;
    bool allow_shadow_debug;
    
    CameraUBO current_camera_ubo;

    std::array<ShadowCascade, shadow_cascade_count> shadow_cascades;
    uint64_t shadow_frame = 0;
    RBImageHandle shadow_atlas_image{};     // a recreated atlas renders every cascade

    
    PipelineObject* shadow_debug_pipeline;
    PipelineObject* tonemap_pipeline;
    PipelineObject* wireframe_pipeline;
    PipelineObject* ssr_pipeline;
    PipelineObject* ssr_composite_pipeline;
    PipelineObject* rtx_gi_pipeline;
    PipelineObject* rtx_gi_reproject_pipeline;
    PipelineObject* rtx_gi_temporal_accum_pipeline;
    PipelineObject* rtx_gi_moments_pipeline;
    PipelineObject* rtx_gi_spatial_filter_pipeline;
    PipelineObject* lighting_pipeline;
    PipelineObject* skinning_pipeline = nullptr;
    PipelineObject* wireframe_mesh_pipeline = nullptr;
    PipelineObject* skeleton_pipeline = nullptr;
    PipelineObject* debug_lines_pipeline = nullptr;

    
    std::shared_ptr<PipelineFamily> tonemap_pipeline_family;
    std::shared_ptr<PipelineFamily> shadow_debug_pipeline_family;
    std::shared_ptr<PipelineFamily> wireframe_pipeline_family;
    std::shared_ptr<PipelineFamily> ssr_pipeline_family;
    std::shared_ptr<PipelineFamily> ssr_composite_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_reproject_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_temporal_accum_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_moments_pipeline_family;
    std::shared_ptr<PipelineFamily> rtx_gi_spatial_filter_pipeline_family;
    std::shared_ptr<PipelineFamily> lighting_pipeline_family;
    std::shared_ptr<PipelineFamily> skinning_pipeline_family;
    std::shared_ptr<PipelineFamily> wireframe_mesh_pipeline_family;
    std::shared_ptr<PipelineFamily> skeleton_pipeline_family;
    std::shared_ptr<PipelineFamily> debug_lines_pipeline_family;
    
    bool use_swapchain_extent;
    
    using PassBatches = std::vector<DrawBatch>;
    std::unordered_map<Name, PassBatches> prepared_batches;
    
    
    
    Engine* engine;
    
    
    RBVertexBufferHandle debug_line_buffer;
    uint8_t*     debug_line_mapped = nullptr;

    uint32_t debug_line_capacity = 19440*32;
    
    [[=rh::serialize]] bool readback_nn = false;
    
    // read from the renderer int params every frame (see DebugViewParams)
    DebugViewMode view_mode = DebugViewMode::LIT;
    bool show_skeleton = false;

    debug_draw::Frame debug_draw_frame;
    std::vector<LineVertex> debug_line_vertices;
    
    NNDenoiserState nn_denoiser_state;
    
    // runtime baked reflection probes, main graph only (pass "ReflectionProbes")
    std::unique_ptr<ReflectionProbeSystem> reflection_probes;

    // sky, clouds and fog, main graph only (passes "CloudShadow", "SkyMarch", "Sky", "FogMarch", "Fog")
    std::unique_ptr<SkyRenderer> sky;

    // sprites of the particle systems, main graph only (pass "Particles")
    std::unique_ptr<ParticleRenderer> particles;

    // mesh draws of the frame: records + indirect commands
    DrawList draw_list;

    // Draws `items` of the pass: grouped by pipeline, one indirect draw per group (in_order: one draw per item
    // in the given order, for passes which blend). bind_resources binds the pass resources after a pipeline.
    void draw_items(RenderGraphContext& ctx, const std::vector<const RenderPrimitive*>& items, uint32_t debug_id,
        bool in_order, const std::function<void(const RenderPrimitivePassInfo&)>& bind_resources);
};
RH_OBJECT(GenericRenderGraph)