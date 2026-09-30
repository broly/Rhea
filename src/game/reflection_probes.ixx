module;

#include <json/value.h>

export module game:reflection_probes;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render;
import render_scene;
import glm;
import name;
import cvar;
import rhmath;
import :draw_list;
#include "object/object_reflection_macro.h"

// Push constants of the full screen probe passes (probe_sky / probe_prefilter / probe_irradiance.frag)
struct ProbeFacePushConstants
{
    uint32_t face;
    uint32_t mip;
    float roughness;
    uint32_t sample_count;
    float blend;        // crossfade weight of the new capture (blend pipelines), 1 otherwise
};
RH_REGISTER_TYPE(ProbeFacePushConstants)

export cvar::Var<bool> cv_probes_enabled(
    "render.probes.enabled", true, "Runtime reflection probes (off: sky ambient only)");
export cvar::Var<int> cv_probes_faces_per_frame(
    "render.probes.faces_per_frame", 1, "Cube faces a rebaking probe renders per frame (1-6). A probe without any bake yet always renders all 6");
export cvar::Var<bool> cv_probes_auto_rebake(
    "render.probes.auto_rebake", true, "Rebake probes when the lights visible to them or the sky change (sun, static lights, weather)");
export cvar::Var<int> cv_probes_blend_frames(
    "render.probes.blend_frames", 30, "Frames a rebaked probe crossfades from its old capture to the new one (1: switch at once)");
export cvar::Var<bool> cv_probes_parallax(
    "render.probes.parallax", true, "Box projected (parallax corrected) probe reflections");
export cvar::Var<bool> cv_probes_diffuse(
    "render.probes.diffuse", true, "Diffuse irradiance from the probes (off: no indirect diffuse)");
export cvar::Var<bool> cv_probes_specular(
    "render.probes.specular", true, "Specular reflections from the probe cubemaps (where SSR misses)");
export cvar::Var<bool> cv_ssr_enabled(
    "render.ssr.enabled", true, "Screen space reflections (replace the probe reflections where the ray hits)");
export cvar::Var<float> cv_ssr_intensity(
    "render.ssr.intensity", 1.0f, "Scale of the screen space reflections");
export cvar::Var<float> cv_probes_diffuse_intensity(
    "render.probes.diffuse_intensity", 1.0f, "Scale of the probes' diffuse irradiance");
export cvar::Var<float> cv_probes_specular_intensity(
    "render.probes.specular_intensity", 1.0f, "Scale of the probes' specular reflections");
export cvar::Var<bool> cv_probes_sky(
    "render.probes.sky", true, "Sky probe: a capture of the sky alone, lights and reflects where no probe is near (off: flat sky ambient)");
export cvar::Var<float> cv_probes_sky_distance(
    "render.probes.sky_distance", 12.0f, "Distance to the probe boxes (m) where the sky probe weighs as much as a probe");
export cvar::Var<int> cv_probes_sky_interval(
    "render.probes.sky_interval", 4, "Least frames between two updates of the sky probe (2-60). It updates as soon as the sun or the sky changes, the probes of the scene wait that frame");
export cvar::Var<bool> cv_probes_show_volumes(
    "render.probes.show_volumes", false, "Draw probe boxes and capture points", {}, cvar::none);

// Runtime baked reflection probes (entities with ReflectionCapture, SceneViewProcessor_ReflectionCapture).
//
// Baking is time sliced inside the main frame (pass "ReflectionProbes", after the shadow map): the probe with
// the highest priority renders `faces_per_frame` faces of the capture cube per frame (static opaque PBR
// geometry, sun + shadow map, point lights, sky, and ambient from the probes baked so far: every rebake adds
// one light bounce). Only lights with Light::visible_in_reflection_probes are baked (the sun with its shadow
// map, the nearest static point lights of the probe): dynamic lights stay out of the probes.
// After the 6th face the capture gets its mip chain and is filtered into the probe's slot: GGX prefiltered
// specular (roughness per mip) and a small irradiance cube. Nothing is read back, no frame stalls.
//
// Crossfade: a probe that already shows a capture is not switched at once. The new capture is filtered once
// into scratch cubes, then every fade_update_interval frames the scratch is alpha blended over the slot
// (one texel read per texel) with weight 1 / (updates left), which moves the slot linearly from the old
// capture to the new one over blend_frames (fixed function blending: the lighting shaders stay as they
// are). The next probe is captured meanwhile, it is filtered once the fade is over.
//
// Priority: probes without a bake, then the dirty probes by staleness over distance to the camera. Dirty:
// moved / (re)activated, the lights visible to probes or the sky changed, update_interval elapsed, camera entered the box
// (rebake_on_enter), request_rebake_all.
//
// The sky probe (sky_slot) stays out of that queue: it lights what no box covers, and the direct light of
// the sun changes at once, so a sky waiting for its turn and fading in shows as the terrain going dark and
// brightening over seconds. When the sun or the sky changes it renders its 6 faces (sky only, its own capture
// cube) and is filtered straight into its slot within one frame, at most every sky_interval frames. The
// capture UBO and the filter source are one per frame: the probes of the scene do nothing in that frame.
//
// Shading side (resources/reflection.glsl): every slot is bound as samplerCube arrays, the UBO carries the
// boxes (influence + parallax proxy) of baked probes.
export class ReflectionProbeSystem
{
public:
    static constexpr uint32_t capture_size = 128;
    static constexpr uint32_t specular_size = 128;
    static constexpr uint32_t specular_mips = 6;       // 128 .. 4: roughness 0, 0.2, .. 1
    static constexpr uint32_t irradiance_size = 16;
    static constexpr uint32_t prefilter_samples = 64;
    static constexpr uint32_t fade_update_interval = 3;   // frames between crossfade updates of a slot
    static constexpr uint32_t irradiance_samples = 128;
    // The last slot is the sky probe: the sky alone seen from the camera (no geometry, no box, no parallax).
    // What no probe box covers is shared between it and the probes (reflection_probe_weights): puddles in
    // the open reflect the sky and its clouds.
    static constexpr uint32_t sky_slot = kMaxReflectionProbes - 1;
    static constexpr float capture_near = 0.05f;
    static constexpr float capture_far = 500.0f;

    struct FrameInputs
    {
        glm::vec3 camera_position;
        double time = 0.0;
        RBImageHandle brdf_lut;
        // from the SkyRenderer: average radiance of the sky (the ambient of what no probe covers), and the
        // cloud coverage - the probes capture the sky, a change of either makes them dirty
        glm::vec3 sky_ambient = glm::vec3(0.0f);
        float cloud_coverage = 0.0f;
    };

    struct ProbeStatus
    {
        Name name;
        uint32_t slot = 0;
        bool active = false;
        bool baked = false;
        bool dirty = false;
        bool baking = false;
        uint32_t bake_count = 0;
        double age_seconds = 0.0;   // since the last finished bake
    };

    void init(Renderer& renderer, RenderBackend& backend);

    // CPU part, from prepare_resources: picks this frame's work and writes the probe UBO / descriptors of
    // the "reflection" resource (read by lighting) and of "probe_capture"
    void prepare(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs);

    // records the work picked by prepare (pass "ReflectionProbes"): own render passes and barriers.
    // Capture draws go through the frame's draw list (one indirect draw per face)
    void execute(RenderGraphContext& ctx, SceneView& scene_view, DrawList& draw_list);

    void request_rebake_all() { rebake_all_requested = true; }

    std::vector<ProbeStatus> get_status() const;
    uint64_t get_faces_rendered() const { return faces_rendered; }

private:
    struct SlotState
    {
        bool valid = false;               // the slot holds a filtered capture
        uint32_t baked_version = ~0u;     // RenderObject_ReflectionCapture::capture_version of that capture
        glm::vec3 baked_position = glm::vec3(0.0f);
        glm::vec3 baked_sun_direction = glm::vec3(0.0f);
        glm::vec3 baked_sun_color = glm::vec3(0.0f);
        glm::vec4 baked_sky = glm::vec4(0.0f);   // FrameInputs: sky_ambient, cloud_coverage
        uint64_t baked_lights_hash = 0;    // static point lights visible to probes
        double bake_time = 0.0;
        uint32_t bake_count = 0;
        bool force = false;               // rebake requested (rebake all / camera entered)
        bool camera_inside = false;
        bool dirty = false;
        const char* dirty_reason = "";
        bool active = false;
        Name name;
    };

    struct Job
    {
        uint32_t slot = 0;
        uint32_t capture_version = 0;
        uint32_t next_face = 0;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 sun_direction = glm::vec3(0.0f);
        glm::vec3 sun_color = glm::vec3(0.0f);
        glm::vec4 sky = glm::vec4(0.0f);
        uint64_t lights_hash = 0;
        uint32_t capture_index = 0;   // capture cube the faces go to
    };

    // a finished capture going into its slot: filtered (into the slot at once, or into the scratch cubes),
    // then blended from the scratch over several updates
    struct Fade
    {
        uint32_t slot = 0;
        uint32_t capture_index = 0;   // source capture cube
        bool blend = false;           // false: filtered straight into the slot (it showed nothing yet)
        bool filtered = false;
        uint32_t step = 0;            // blend updates done
        uint32_t steps = 1;
        uint32_t cooldown = 0;        // frames until the next blend update
    };

    // lights of the scene the probes see (Light::visible_in_reflection_probes)
    struct ProbeLights
    {
        bool sun_visible = false;
        glm::vec3 sun_direction = glm::vec3(0.0f, 1.0f, 0.0f);   // towards the sun
        glm::vec3 sun_color = glm::vec3(0.0f);
        std::vector<std::pair<glm::vec3, glm::vec3>> points;     // position, color
        uint64_t points_hash = 0;
    };

    // what execute records this frame
    struct FrameWork
    {
        // capture
        uint32_t slot = 0;
        uint32_t capture_index = 0;
        glm::vec3 position = glm::vec3(0.0f);   // capture point
        uint32_t first_face = 0;
        uint32_t face_count = 0;
        // filter of a finished capture (mip chain, GGX prefilter, irradiance): into the slot or the scratch
        bool filter = false;
        bool filter_to_scratch = false;
        uint32_t filter_slot = 0;
        uint32_t filter_capture_index = 0;
        // crossfade update: scratch blended over the slot
        bool blend = false;
        uint32_t blend_slot = 0;
        float blend_alpha = 1.0f;
        // the sky probe: all faces into sky_capture, filtered into its slot (nothing of the above then)
        bool sky = false;
    };

    void gather_lights(SceneView& scene_view);
    void update_slots(SceneView& scene_view, const FrameInputs& inputs);
    // true: the sky probe takes this frame
    bool plan_sky(const FrameInputs& inputs);
    void plan_work(SceneView& scene_view, const FrameInputs& inputs);
    void write_descriptors(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs);
    void draw_debug_volumes(SceneView& scene_view) const;

    void initialize_images(RenderGraphContext& ctx);
    void render_face(RenderGraphContext& ctx, SceneView& scene_view, DrawList& draw_list, RBImageHandle capture_color,
        uint32_t face, bool sky_only);
    void filter_capture(RenderGraphContext& ctx, RBImageHandle source, RBImageHandle specular, RBImageHandle irradiance);
    void blend_into_slot(RenderGraphContext& ctx, uint32_t slot, float alpha);
    void draw_filter_face(RenderGraphContext& ctx, Name pass, RBLoadOp load, RBImageHandle target, Name target_name,
        uint32_t size, uint32_t face, uint32_t mip, PipelineObject* pipeline, const ProbeFacePushConstants& pc);
    void transition(RenderGraphContext& ctx, RBImageHandle image, RBImageUsageType usage,
        uint32_t base_layer = 0, uint32_t layer_count = 0, uint32_t base_mip = 0, uint32_t mip_count = 0) const;

    static glm::mat4 face_view_proj(uint32_t face, glm::vec3 position);

    Renderer* renderer = nullptr;
    RenderBackend* backend = nullptr;

    RenderResource* reflection_resource = nullptr;
    RenderResource* probe_capture_resource = nullptr;
    RenderResource* sky_resource = nullptr;
    RenderResource* light_resource = nullptr;
    RenderResource* shadow_resource = nullptr;
    RenderResource* mesh_table_resource = nullptr;
    RenderResource* primitive_table_resource = nullptr;
    RenderResource* pbr_material_table_resource = nullptr;
    RenderResource* textures_resource = nullptr;

    std::shared_ptr<PipelineFamily> capture_family;
    std::shared_ptr<PipelineFamily> sky_family;
    std::shared_ptr<PipelineFamily> prefilter_family;
    std::shared_ptr<PipelineFamily> irradiance_family;
    std::shared_ptr<PipelineFamily> blend_family;
    // base pass family of the "pbr" model: the primitives the capture can shade (material table layout)
    std::shared_ptr<PipelineFamily> pbr_base_family;
    PipelineObject* capture_pipeline = nullptr;
    PipelineObject* sky_pipeline = nullptr;
    PipelineObject* prefilter_pipeline = nullptr;
    PipelineObject* irradiance_pipeline = nullptr;
    PipelineObject* blend_pipeline = nullptr;

    // cubes with mips, the sources of the filters: one is captured while the other fades into its slot
    std::array<RBImageHandle, 2> capture_colors;
    // the sky probe's own capture cube: captured and filtered within one frame, whatever the other two hold
    RBImageHandle sky_capture;
    RBImageHandle capture_depth;
    // a filtered capture waiting to be blended into its slot
    RBImageHandle scratch_specular;
    RBImageHandle scratch_irradiance;
    std::array<RBImageHandle, kMaxReflectionProbes> specular_images;
    std::array<RBImageHandle, kMaxReflectionProbes> irradiance_images;
    bool images_initialized = false;

    std::array<SlotState, kMaxReflectionProbes> slots;
    std::optional<Job> job;
    std::optional<Fade> fade;
    uint32_t sky_cooldown = 0;   // frames until the sky probe may update again
    ProbeLights probe_lights;
    FrameWork frame_work;
    bool rebake_all_requested = false;
    bool warned_slot_overflow = false;
    double current_time = 0.0;
    uint64_t faces_rendered = 0;
};
