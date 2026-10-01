module;

#include <json/value.h>

export module foliage;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import dependency_collector;
import fixed_string;
import type_id;
import ecs;
import framework;
import assets;
import render_scene;
import terrain;
#include "object/object_reflection_macro.h"

// Ground foliage of a terrain: tufts of grass and flowers (scanned models, tools/foliage), grown on the GPU around
// the camera every frame.
//
// What: a foliage asset (assets/foliage/<source>.json, written by tools/foliage/import_foliage.py) is a set of
// variants of one plant - tufts of one grass, rosettes of dandelions - each with 3 LOD meshes, sharing a
// "foliage" material. A FoliageType picks variants of an asset and says how they grow.
//
// Where: the terrain's foliage map (Terrain::foliagemap, RGBA8, painted with the Foliage brush of the Terrain
// window or written by tools/foliage/generate_foliage_mask.py): every type grows on one channel, the channel's
// value scales its density. Several types may share a channel. Patches (patch_scale) break a type into clumps.
//
// How (game:foliage_renderer, passes FoliageCull and GroundFoliage): a jittered world grid of candidate spots per
// type, spaced for its density, covers the camera's surroundings up to fade_end; a compute shader keeps the spots
// the mask, the patches and the distance fade allow, culls them by the frustum and the hierarchical depth, picks a
// variant and a LOD and appends the plant to that bucket; one indexed indirect draw per bucket. The vertex shader
// bends the plants in the wind (gusts travel along wind_heading) and away from characters. The farther plants thin
// out between fade_start and fade_end (each one at its own distance in that range) and shrink before they go: no
// ring where the foliage ends. cvars render.foliage.* scale distances and density.
//
//     "TerrainFoliage": { "types": [ { "name": "meadow_grass", "asset": "foliage/grass_medium_01.json",
//                                      "variants": ["tall_a", "tall_b"], "channel": 0, "density": 6, ... } ] }
//
// Must be on the entity of the Terrain.
export
{
    struct FoliageVariant
    {
        std::string name;
        std::vector<MeshHandle> lods;           // 3: LOD 0, 1, 2 (the same mesh may repeat)
        float height = 0.1f;                    // m, of the model
        float radius = 0.1f;                    // m, around its origin (xz)
        std::vector<uint32_t> triangles;
    };

    struct FoliageAsset
    {
        std::string source;
        std::shared_ptr<Material> material;     // model "foliage"
        std::vector<FoliageVariant> variants;
    };

    struct FoliageType
    {
        std::string name;
        [[=rh::edit, =rh::read_only]] std::string asset;                // foliage asset (assets/...)
        std::vector<std::string> variants;                              // of the asset, by name; empty: all
        [[=rh::edit, =rh::range<0.f, 3.f>]] uint32_t channel = 0;      // of the foliage map: R G B A
        [[=rh::edit, =rh::range<0.f, 60.f>]] float density = 4.0f;     // plants per m2 where the channel is full
        // channel values below it grow nothing; above it the density rises to full at 1
        [[=rh::edit, =rh::range<0.f, 1.f>]] float mask_threshold = 0.05f;
        // m, size of noise patches the type grows in (0: everywhere the mask allows), and the share of the ground
        // they cover
        [[=rh::edit, =rh::range<0.f, 100.f>]] float patch_scale = 0.0f;
        [[=rh::edit, =rh::range<0.f, 1.f>]] float patch_coverage = 1.0f;

        // m from the camera: plants thin out between them (every one has its own distance) and are gone at fade_end
        [[=rh::edit, =rh::range<0.f, 300.f>]] float fade_start = 20.0f;
        [[=rh::edit, =rh::range<0.f, 300.f>]] float fade_end = 35.0f;
        // m: coarser meshes beyond them
        [[=rh::edit, =rh::range<0.f, 300.f>]] float lod1_distance = 8.0f;
        [[=rh::edit, =rh::range<0.f, 300.f>]] float lod2_distance = 18.0f;

        // plant: scale of the model (random in the range), m it is sunk into the ground, how much it tilts with
        // the slope (0: upright, 1: along the ground's normal)
        [[=rh::edit, =rh::range<0.f, 10.f>]] float scale_min = 1.0f;
        [[=rh::edit, =rh::range<0.f, 10.f>]] float scale_max = 1.0f;
        [[=rh::edit, =rh::range<0.f, 0.2f>]] float sink = 0.01f;
        [[=rh::edit, =rh::range<0.f, 1.f>]] float ground_align = 0.3f;
        [[=rh::edit, =rh::range<0.f, 3.f>]] float wind = 1.0f;          // response to the wind

        // color: a tint of the texture, a random brightness / hue variation per plant, a share of plants turned
        // toward dry_color
        [[=rh::edit, =rh::color]] vec3 tint{ 1.0f, 1.0f, 1.0f };
        [[=rh::edit, =rh::range<0.f, 1.f>]] float tint_variation = 0.15f;
        [[=rh::edit, =rh::color]] vec3 dry_color{ 0.55f, 0.48f, 0.3f };
        [[=rh::edit, =rh::range<0.f, 1.f>]] float dry_fraction = 0.0f;
        [[=rh::edit, =rh::range<0.f, 1.f>]] float color_noise = 0.25f;  // large scale brightness / yellow patches
        [[=rh::edit, =rh::range<0.f, 3.f>]] float transmission = 1.0f; // x the material's transmission

        [[=rh::edit]] uint32_t seed = 0;                                 // of the placement and the patches

        // the asset and the indices of the variants picked from it (set at spawn)
        [[=rh::transient]] std::shared_ptr<const FoliageAsset> loaded;
        [[=rh::transient]] std::vector<uint32_t> variant_indices;
    };

    struct TerrainFoliage
    {
        [[=rh::edit]] bool enabled = true;
        std::vector<FoliageType> types;

        // wind: a steady lean plus gusts rolling along the heading
        [[=rh::edit, =rh::range<0.f, 2.f>]] float wind_strength = 0.35f;
        [[=rh::edit, =rh::degrees]] float wind_heading = 0.5f;          // radians from +x toward +z
        [[=rh::edit, =rh::range<0.f, 20.f>]] float gust_speed = 4.0f;   // m/s
        [[=rh::edit, =rh::range<1.f, 100.f>]] float gust_scale = 14.0f; // m
        [[=rh::edit, =rh::range<0.f, 2.f>]] float gust_strength = 0.6f;

        // characters push the plants aside within this radius (m)
        [[=rh::edit, =rh::range<0.f, 2.f>]] float interaction_radius = 0.6f;
    };

    // One of the interactors that push the plants aside (characters)
    struct FoliageInteractor
    {
        glm::vec3 position{ 0.0f };     // world, feet
        float radius = 0.0f;
    };

    // Render side: what the foliage renderer grows this frame (filled by the foliage module in Late)
    class SceneViewProcessor_Foliage : public SceneViewProcessor
    {
    public:
        void process() override {}

        bool valid = false;                     // a spawned terrain with an enabled TerrainFoliage
        std::shared_ptr<const TerrainData> terrain;
        std::vector<FoliageType> types;         // the ones with a loaded asset
        float wind_strength = 0.0f;
        float wind_heading = 0.0f;
        float gust_speed = 0.0f;
        float gust_scale = 1.0f;
        float gust_strength = 0.0f;
        double time = 0.0;                      // s, world
        std::vector<FoliageInteractor> interactors;
    };
}
RH_OBJECT(SceneViewProcessor_Foliage)
