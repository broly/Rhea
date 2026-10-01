module;

#include <json/value.h>

export module game:foliage_renderer;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render;
import render_scene;
import glm;
import cvar;
import rhmath;
#include "object/object_reflection_macro.h"

// foliage_cull.comp
struct FoliageCullPushConstants
{
    uint32_t mode;              // 0: reset the draws, 1: grow a type
    uint32_t type;
    int32_t first_cell_x;
    int32_t first_cell_z;
    uint32_t cells;             // per side
    uint32_t bucket_count;
};
RH_REGISTER_TYPE(FoliageCullPushConstants)

export cvar::Var<bool> cv_foliage_enabled(
    "render.foliage.enabled", true, "Ground foliage of the terrain (grass, flowers) is drawn");
export cvar::Var<float> cv_foliage_distance(
    "render.foliage.distance", 1.0f, "Multiplies the fade and LOD distances of every ground foliage type",
    { .has_range = true, .min = 0.1f, .max = 4.0f });
export cvar::Var<float> cv_foliage_density(
    "render.foliage.density", 1.0f, "Multiplies the density of every ground foliage type",
    { .has_range = true, .min = 0.05f, .max = 4.0f });
export cvar::Var<bool> cv_foliage_occlusion(
    "render.foliage.occlusion", true, "Ground foliage hidden behind the early base pass draws is culled (hierarchical depth)");

// resources/foliage.glsl: FoliageGlobals
struct GPUFoliageGlobals
{
    glm::vec4 terrain;
    glm::vec4 terrain_info;
    glm::vec4 frustum[6];
    glm::vec4 camera;
    glm::vec4 wind;
    glm::vec4 gust;
    glm::uvec4 hzb;
    glm::uvec4 counts;
    glm::vec4 interactors[4];
};

// resources/foliage.glsl: FoliageType
struct GPUFoliageType
{
    glm::vec4 placement;
    glm::vec4 fade;
    glm::vec4 mask;
    glm::vec4 size;
    glm::vec4 bounds;
    glm::vec4 tint;
    glm::vec4 dry;
    glm::vec4 color;
    glm::uvec4 buckets;
};

// resources/foliage.glsl: FoliageBucket
struct GPUFoliageBucket
{
    glm::uvec4 draw;
    glm::uvec4 mesh;
    glm::vec4 size;
};
static_assert(sizeof(GPUFoliageGlobals) == 272 && sizeof(GPUFoliageType) == 144 && sizeof(GPUFoliageBucket) == 48,
    "std430 layout of resources/foliage.glsl");

// Ground foliage of the main view (TerrainFoliage, module foliage: settings, SceneViewProcessor_Foliage):
//
//   prepare  the type table (densities, distances x the cvars) and the buckets: one per variant and LOD of a type,
//            its mesh (uploaded on first use), material, index range, and a share of the instance buffer in
//            proportion to what it could hold at most; the frustum, the wind, the characters -> resource
//            "foliage"; the terrain heights when they changed
//   cull     pass FoliageCull (compute, after the hierarchical depth of the early base pass): resets the
//            indirect draws, then one dispatch per type over its candidate grid around the camera
//   draw     pass GroundFoliage (the g-buffer, after the base pass): one indexed indirect draw per bucket
//
// The instances live in GPU only buffers (resource "foliage_instances"); plants beyond a full bucket are dropped.
export class FoliageRenderer
{
public:
    static constexpr uint32_t lods = 3;
    static constexpr uint32_t max_types = 64;

    struct FrameInputs
    {
        glm::mat4 view_proj;
        glm::vec3 camera_position;
        // hierarchical depth of this frame's early base pass (OcclusionCulling), if it is built
        bool hzb_valid = false;
        uint32_t hzb_width = 0;
        uint32_t hzb_height = 0;
        uint32_t hzb_levels = 0;
    };

    struct DrawResources
    {
        RenderResource* camera = nullptr;
        RenderResource* mesh_table = nullptr;
        RenderResource* material_table = nullptr;
        RenderResource* textures = nullptr;
    };

    void init(Renderer& renderer, RenderBackend& backend);

    void prepare(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs);

    // anything to grow this frame (set by prepare)
    bool is_active() const { return active; }

    void cull(RenderGraphContext& ctx, RenderResource* camera, RenderResource* occlusion);
    void draw(RenderGraphContext& ctx, const DrawResources& resources);

private:
    Renderer* renderer = nullptr;
    RenderBackend* backend = nullptr;

    RenderResource* foliage_resource = nullptr;
    RenderResource* instances_resource = nullptr;

    std::shared_ptr<PipelineFamily> cull_family;
    std::shared_ptr<PipelineFamily> draw_family;
    PipelineObject* cull_pipeline = nullptr;
    PipelineObject* draw_pipeline = nullptr;

    struct Dispatch
    {
        uint32_t type = 0;
        glm::ivec2 first_cell{ 0 };
        uint32_t cells = 0;
    };
    std::vector<Dispatch> dispatches;
    // index block of every bucket (the draws bind it)
    std::vector<uint32_t> bucket_blocks;
    uint32_t bucket_count = 0;
    uint32_t bucket_capacity = 0;
    uint32_t instance_capacity = 0;
    uint32_t heights_capacity = 0;          // floats
    uint32_t table_capacity = 0;            // types
    bool active = false;

    // uploaded meshes: mesh table index and index range, by mesh asset
    struct UploadedMesh
    {
        uint32_t mesh_index = 0;
        MeshIndexRange indices;
    };
    std::unordered_map<uint32_t, UploadedMesh> meshes;
    // material table index by material (the instances are kept alive)
    std::unordered_map<const void*, std::shared_ptr<MaterialInstance>> materials;

    // heights copied into each frame's copy of the resource: terrain and its heights_version
    struct HeightsCopy
    {
        const void* terrain = nullptr;
        uint32_t version = 0;
    };
    std::array<HeightsCopy, 8> heights_copies{};
    float previous_time = -1.0f;            // s: the wind of the last frame (motion vectors)
    bool size_reported = false;
};
