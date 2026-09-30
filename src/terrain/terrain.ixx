module;

#include <json/value.h>

export module terrain;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import assets;
import physics;

// Heightmap terrain:
//  * rendered as chunk meshes (child entities with a MeshRenderer) with the "terrain" material model:
//    four PBR layers (base color + height, normal, ORM) blended by a splat map (RGBA = layer weights),
//  * static height field collision (Jolt), walkable by characters,
//  * editable at runtime: layer painting and sculpting with a brush (Terrain window of the debug UI), Save.
//
// Centered on the entity's position; its rotation and scale are ignored. Sample (x, z) lies at
// position + (x * spacing - size / 2, height, z * spacing - size / 2), size = (samples - 1) * spacing.
//
//     "Terrain": { "heightmap": "terrain/valley.r16", "splatmap": "terrain/valley_splat.png",
//                  "samples": 512, "spacing": 1.0, "min_height": -20, "max_height": 80,
//                  "material": { "model": "terrain", "parameters": { ... } } }
export
{
    struct TerrainData;

    struct Terrain
    {
        // raw little endian uint16 samples x samples, mapped onto [min_height, max_height];
        // no file: flat at 0 (Save writes it)
        [[=rh::edit, =rh::read_only]] std::string heightmap;
        // PNG, RGBA = weights of layers 0..3; no file: layer 0 everywhere (Save writes it)
        [[=rh::edit, =rh::read_only]] std::string splatmap;
        // PNG, R = water level of the puddles (terrain.frag); no file: dry (Save writes it)
        [[=rh::edit, =rh::read_only]] std::string puddlemap;

        [[=rh::edit, =rh::read_only]] uint32_t samples = 512;             // per side, a multiple of 4
        [[=rh::edit, =rh::read_only]] float spacing = 1.0f;               // m between samples
        [[=rh::edit, =rh::read_only]] float min_height = -20.0f;          // m, relative to the entity
        [[=rh::edit, =rh::read_only]] float max_height = 80.0f;
        [[=rh::edit, =rh::read_only]] uint32_t splat_resolution = 2048;   // of a new splat map
        [[=rh::edit, =rh::read_only]] uint32_t puddle_resolution = 1024;  // of a new puddle map
        [[=rh::edit, =rh::read_only]] uint32_t chunk_quads = 64;         // per side of a chunk mesh

        // names of the layers in the editor (material slots 0..3)
        std::vector<std::string> layer_names = { "layer 0", "layer 1", "layer 2", "layer 3" };

        // where the Terrain window's "Teleport player" button puts the player (x, z; y is the terrain's)
        [[=rh::edit]] vec3 teleport_spot{ 0.0f, 0.0f, 0.0f };

        std::shared_ptr<Material> material;     // model "terrain"; its "splat" texture is set at spawn

        [[=rh::transient]] std::shared_ptr<TerrainData> data;
    };

    struct TerrainChunk
    {
        ecs::Entity entity;
        MeshHandle mesh;
        uint32_t first_x = 0;       // first sample
        uint32_t first_z = 0;
        uint32_t quads_x = 0;
        uint32_t quads_z = 0;
    };

    // Samples, x / z inclusive
    struct TerrainRect
    {
        int32_t min_x = std::numeric_limits<int32_t>::max();
        int32_t min_z = std::numeric_limits<int32_t>::max();
        int32_t max_x = std::numeric_limits<int32_t>::min();
        int32_t max_z = std::numeric_limits<int32_t>::min();

        bool empty() const { return min_x > max_x || min_z > max_z; }
        void add(const TerrainRect& other)
        {
            min_x = std::min(min_x, other.min_x);
            min_z = std::min(min_z, other.min_z);
            max_x = std::max(max_x, other.max_x);
            max_z = std::max(max_z, other.max_z);
        }
        bool intersects(const TerrainRect& other) const
        {
            return !empty() && !other.empty() && min_x <= other.max_x && other.min_x <= max_x
                && min_z <= other.max_z && other.min_z <= max_z;
        }
    };

    // Runtime state of a Terrain (Terrain::data)
    struct TerrainData
    {
        uint32_t samples = 0;
        float spacing = 1.0f;
        float min_height = 0.0f;
        float max_height = 0.0f;
        glm::vec3 origin{ 0.0f };                   // world position of sample (0, 0) at height 0

        std::vector<float> heights;                 // samples^2, relative to the entity, rows along x
        TextureHandle splat;                        // CPU copy is AssetManager's (terrain::get_splat)
        TextureHandle puddles;                      // RGBA8, R = water level (terrain::get_puddles)
        std::vector<TerrainChunk> chunks;

        phys::Shape shape;
        phys::BodyId body;

        // edits not uploaded yet (terrain::commit_render), collision not rebuilt (terrain::commit_collision)
        TerrainRect dirty_heights;
        TerrainRect dirty_splat;                    // texels
        TerrainRect dirty_puddles;                  // texels
        bool collision_dirty = false;
        bool unsaved = false;

        float size() const { return float(samples - 1) * spacing; }
    };

    enum class TerrainBrushMode : uint8_t
    {
        paint,      // layer weights toward `layer`
        raise,
        lower,
        smooth,     // toward the average of the neighbors
        flatten,    // toward flatten_height
        water,      // puddle water level up, to the shape of the brush
        dry,        // and down
    };

    struct TerrainBrush
    {
        TerrainBrushMode mode = TerrainBrushMode::paint;
        float radius = 4.0f;            // m
        float strength = 0.5f;          // 0..1
        float falloff = 0.6f;           // 0: hard edge, 1: fades out from the center
        uint32_t layer = 0;             // paint
        float flatten_height = 0.0f;    // flatten: world height
    };

    namespace terrain
    {
        // Height of the terrain surface at a world position (bilinear), nullopt outside of it
        std::optional<float> height_at(const TerrainData& data, glm::vec2 world_xz);

        // First hit of a ray with the height field (current heights, CPU), nullopt when it misses
        std::optional<glm::vec3> raycast(const TerrainData& data, const glm::vec3& origin, const glm::vec3& direction,
                                         float max_distance = 1000.0f);

        // One step of a brush stroke centered at world_xz, over dt seconds (strength is per second).
        // Changes show up after commit_render; sculpting also needs commit_collision.
        void apply_brush(TerrainData& data, const TerrainBrush& brush, glm::vec2 world_xz, float dt);

        // Uploads the edited heights (chunk meshes) and weights (splat texture) to the GPU. Waits for the GPU:
        // editor only.
        void commit_render(TerrainData& data);

        // Rebuilds the height field body from the edited heights (after a stroke)
        void commit_collision(TerrainData& data, phys::PhysicsScene& physics, ecs::Entity owner);

        // Writes the heightmap and the splat map (paths of the Terrain, under assets/)
        bool save(const Terrain& terrain, std::string& error);

        // The splat map texels (RGBA8, splat_resolution^2), AssetManager's copy: uploaded from it
        Texture& get_splat(const TerrainData& data);
        Texture& get_puddles(const TerrainData& data);
    }
}
