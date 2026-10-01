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
import :foliage_renderer;
import :occlusion_culling;
import :gtao_renderer;
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

// Soft shadows of the sun (shaders/resources/shadow.glsl, PCSS): the penumbra grows with the distance from the
// blocker, so a mountain a few km away casts a shadow with an edge tens of metres wide
export cvar::Var<float> cv_shadow_sun_angle(
    "render.shadows.sun_angle", 0.53f, "Angular diameter of the sun for the shadow penumbra, degrees (0: fixed 3x3 PCF)",
    { .has_range = true, .min = 0.0f, .max = 5.0f });
export cvar::Var<float> cv_shadow_max_penumbra(
    "render.shadows.max_penumbra", 12.0f, "Largest shadow penumbra radius, texels of a cascade",
    { .has_range = true, .min = 1.0f, .max = 32.0f });

// Simplified shadow casters (MeshRenderer::shadow_proxies): a cascade draws the coarsest proxy whose error is at
// most this many of its texels (0: the meshes themselves)
export cvar::Var<float> cv_shadow_proxy_texels(
    "render.shadows.proxy_texels", 1.0f, "Shadow proxies: allowed error in texels of the cascade (0: off)",
    { .has_range = true, .min = 0.0f, .max = 8.0f });

// LOD switch distances of every mesh are multiplied by it (MeshRenderer::lods): the imported distances keep
// several triangles per pixel at 2560x1506, below 1 the coarser levels take over closer
export cvar::Var<float> cv_lod_scale(
    "render.lod_scale", 0.5f, "Multiplies the LOD distances of every mesh (below 1: coarser levels closer)",
    { .has_range = true, .min = 0.1f, .max = 4.0f });

// A cascade draws its casters in the LOD of the distance where it starts (the far cascades shade far receivers)
export cvar::Var<bool> cv_shadow_cascade_lods(
    "render.shadows.cascade_lods", true, "Shadow cascades draw casters at least in the LOD of their start distance");

// Screen space reflections at a fraction of the resolution: the rays are traced for one pixel of every
// downscale x downscale block, SSRComposite upsamples the result
export cvar::Var<int> cv_ssr_downscale(
    "render.ssr.downscale", 2, "Screen pixels per SSR texel, per axis (1: full resolution)",
    { .has_range = true, .min = 1.0f, .max = 4.0f });

// Casters whose bounds are smaller than this many texels of a cascade are left out of it
export cvar::Var<float> cv_shadow_min_caster_texels(
    "render.shadows.min_caster_texels", 2.0f, "Shadow casters smaller than this many texels of the cascade are skipped",
    { .has_range = true, .min = 0.0f, .max = 16.0f });

// Performance experiments (tools/bench/run_pose.py --a / --b): meshes left out of a pass by name, to see what
// they cost. Comma separated parts of mesh names, '*' is every mesh, '-part' keeps the meshes with that part:
// "*,-.glb,-exterior" hides the Sponza interior (its meshes are glTF node names, trees and props are .glb files)
export cvar::Var<std::string> cv_debug_hide_base(
    "render.debug.hide_base", "", "Meshes not drawn by the base pass: name parts, '*' all, '-part' keeps", {}, cvar::none);
export cvar::Var<std::string> cv_debug_hide_shadow(
    "render.debug.hide_shadow", "", "Meshes not drawn into the shadow maps: name parts, '*' all, '-part' keeps", {}, cvar::none);

// A shadow cascade as rendered into its atlas tile: the light UBO carries these, so a tile that was not rendered
// this frame is still sampled with the matrix it was rendered with
export struct ShadowCascade
{
    glm::mat4 vp{ 1.0f };
    glm::vec3 center{ 0.0f };           // world, of the fitted sphere
    float radius = 0.0f;
    float texel = 0.0f;                 // world size of a shadow texel
    float depth_range = 1.0f;           // m between the near and far plane
    float split_near = 0.0f;            // m from the camera where the cascade starts: LODs of its casters from there
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
    uint32_t gtao_downscale = 0;   // screen pixels per texel of the GTAO result, 0: no GTAO this frame
    float gtao_foliage = 1.0f;     // strength of GTAO on leaves
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
    uint32_t ssr_downscale = 1;     // render.ssr.downscale ssr_texture was created with
    RGTextureHandle swapchain_color;
    
    HDROutputTextureArray hdr_color_present;
    HDROutputTextureArray hdr_color_history;
    
    GBufferArray gbuffer;
    GBufferArray gbuffer_hist;
    RGTextureHandle decal_albedo;
    
    RGTextureHandle ssr_texture;
    uint32_t history_index = 0;

    // GTAO at 1 / gtao_downscale of the screen (GtaoRenderer, main graph): raw samples, after the first
    // denoise pass, result (read by the lighting pass; a 1 x 1 texture nobody writes in the other graphs)
    RGTextureHandle gtao_raw;
    RGTextureHandle gtao_mid;
    RGTextureHandle gtao_result;
    uint32_t gtao_downscale = 1;    // render.gtao.downscale the GTAO targets were created with

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
    RenderResource* gtao_resource;
    
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

    // grass and flowers on the terrain, main graph only (passes FoliageCull, GroundFoliage)
    std::unique_ptr<FoliageRenderer> foliage;

    // GPU occlusion culling of the base pass, main graph only (passes OcclusionCullEarly, HZB, OcclusionCullLate,
    // GeometryBaseLate)
    std::unique_ptr<OcclusionCulling> occlusion;

    // screen space ambient occlusion, main graph only (passes GTAO, GTAODenoise1, GTAODenoise2)
    std::unique_ptr<GtaoRenderer> gtao;

    // mesh draws of the frame: records + indirect commands
    DrawList draw_list;

    // One mesh draw: the primitive (transform, material, passes) and the mesh it draws, its own or a stand-in
    // (a simplified shadow caster)
    struct MeshDraw
    {
        const RenderPrimitive* primitive = nullptr;
        uint32_t mesh_index = 0;
        MeshIndexRange indices;
    };
    // the primitives with their own meshes
    static std::vector<MeshDraw> own_meshes(const std::vector<const RenderPrimitive*>& primitives);

    // Draws `items` of the pass: grouped by pipeline and index block, one indirect draw per group (in_order: one
    // draw per item in the given order, for passes which blend). bind_resources binds the pass resources after a
    // pipeline.
    // Draws of one pipeline and index block, commands [first_command, first_command + command_count) of the
    // draw list
    struct DrawGroup
    {
        const RenderPrimitivePassInfo* info = nullptr;
        uint32_t index_block = 0;
        uint32_t first_command = 0;
        uint32_t command_count = 0;
    };
    // Puts the draws of the pass into the draw list, grouped by pipeline and index block (the order of the items is
    // kept inside a group). with_bounds: their world bounds too (for the GPU culling)
    std::vector<DrawGroup> record_groups(Name pass, std::span<const MeshDraw> items, bool with_bounds);
    // One indirect draw per group; `commands` / command_offset: a GPU made copy of the commands (culling)
    void draw_groups(RenderGraphContext& ctx, std::span<const DrawGroup> groups, uint32_t debug_id,
        const std::function<void(const RenderPrimitivePassInfo&)>& bind_resources,
        std::optional<RBBufferHandle> commands = std::nullopt, uint32_t command_offset = 0);

    // The base pass of the main graph, put into the draw list before the passes: the occlusion culling runs over
    // its commands before GeometryBase draws them
    void prepare_base_draws(RenderGraphContext& ctx);
    std::vector<DrawGroup> base_groups;
    uint32_t base_first_command = 0;
    uint32_t base_command_count = 0;
    bool base_draws_prepared = false;

    void draw_items(RenderGraphContext& ctx, std::span<const MeshDraw> items, uint32_t debug_id,
        bool in_order, const std::function<void(const RenderPrimitivePassInfo&)>& bind_resources);

    // `render.dump_draws` (one-time flag dump_draws): for the next frames every mesh pass appends what it draws,
    // by mesh asset and LOD, to cache/bench/draws.csv. Several frames: the far shadow cascades take turns.
    static constexpr uint32_t draw_dump_frame_count = 4;
    uint32_t draw_dump_frames = 0;
    void dump_draws(Name pass, uint32_t debug_id, std::span<const MeshDraw> draws) const;
};
RH_OBJECT(GenericRenderGraph)