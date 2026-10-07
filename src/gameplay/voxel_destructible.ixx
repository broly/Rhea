module;

#include <json/value.h>

export module gameplay:voxel_destructible;

import std.compat;
import glm;
import reflect;
import rhobject;
import ecs;
import framework;
import assets;
import voxel;

// Voxel houses (V5, V4 on the CPU): a mesh entity with a voxel copy that replaces it when a weapon with
// `voxelize` hits it (the house cannon); every later hit of such a weapon recompresses the voxels around it
// (3D JPEG).
//
//   build      once the entity's MeshRenderer has its mesh (update_voxel_copies): the triangles and the base
//              color textures of its opaque materials go to a worker thread, which loads the grid from the voxel
//              cache (cache/voxels/<mesh>_<size>.rvox, keyed by the mesh, the textures and the settings) or
//              voxelizes the mesh (voxel:voxelize), then builds the surface meshes of its chunks (voxel:mesh)
//   chunks     the copy is drawn in chunks of chunk_size^3 voxels: each a hidden child "<name>_voxels_x_y_z" with
//              its own mesh and color atlas (a MeshRenderer of the "pbr" model, the grid is in the mesh's local
//              space). An atlas stays within render.textures.max_size (larger ones are shrunk on upload) and gets
//              room to grow; a hit rebuilds only the chunks it changed
//   voxelized  a WeaponImpactEvent of a weapon with `voxelize` within its radius of the entity's bounds
//              (voxelize_on_impact): the entity's MeshRenderer is hidden, the chunks shown, all flash (HitFlash
//              with the weapon's hit flash). The collision stays the mesh's (bricks: V6).
//   hits       the next impacts of such a weapon queue a VoxelHit (the impact in voxels, the radius, the seed):
//              a worker thread recompresses a copy of the grid with the queued hits in order
//              (voxel::recompress, the weapon's `jpeg` preset of assets/jpeg/presets.json section "voxels") and
//              rebuilds the chunks they changed into atlases of their size; then the grid is replaced, the atlases
//              updated in place (the GPU waits once, like the terrain brushes) and the chunks get their new meshes.
//              A chunk that outgrew its atlas, or a new one, gets a new texture. Hits arriving meanwhile wait for
//              the next job. Replaced meshes and atlases are released (Renderer::release_mesh / release_texture),
//              and all of them when the VoxelDestructible goes.
//
// The grid stays on the CPU (`grid`): the state the 3D JPEG and the integrity (V6) work on, the same on every
// machine for the same hits (integer voxel centers and seeds; the network sends the hits); the GPU only draws
// the meshes built from it.
//
//     "VoxelDestructible": { "voxel_size": 0.05, "fill_thickness": 6, "texels_per_voxel": 2 }
//
// Console: voxel.voxelize (every VoxelDestructible now), voxel.restore (back to the meshes), voxel.hit
// <x y z> [preset] (a hit at a world position, default preset house_cannon).
export
{
    enum class VoxelCopyState : uint8_t
    {
        waiting,        // for the mesh
        building,       // on a worker thread
        ready,          // the voxel copy is there, hidden
        voxelized,      // the voxel copy is shown instead of the mesh
        failed,         // see the log
    };

    // One recompression of a voxel copy: what the network would send
    struct VoxelHit
    {
        glm::ivec3 center{ 0 };         // grid voxel coordinates
        int32_t radius = 0;             // voxels
        uint32_t seed = 0;
        voxel::VoxelJpegSettings settings;
    };

    // The surface mesh of one chunk, built on a worker thread
    struct VoxelChunkMesh
    {
        glm::ivec3 coord{ 0 };          // in chunks
        voxel::SurfaceMesh mesh;        // empty: nothing to draw any more
    };

    // What a worker thread builds, taken over by update_voxel_copies: the first copy, or the result of hits
    struct VoxelCopyBuild
    {
        std::optional<voxel::VoxelGrid> grid;
        std::vector<VoxelChunkMesh> chunks;     // all (first copy) or the ones the hits changed
        std::string error;
        bool cached = false;            // the grid came from the voxel cache
        double voxelize_ms = 0.0;       // or load, or the hits
        double mesh_ms = 0.0;
        uint32_t hits = 0;              // applied (hit jobs)
        voxel::VoxelJpegStats stats;    // of the hits, summed
        std::future<void> done;
    };

    // A drawn chunk of a voxel copy
    struct VoxelChunk
    {
        glm::ivec3 coord{ 0 };          // in chunks
        ecs::Entity entity;             // the child with the MeshRenderer
        MeshHandle mesh;                // its mesh: released with the atlas when replaced or when the copy goes
        TextureHandle atlas;
        uint32_t quads = 0;
    };

    struct VoxelDestructible
    {
        static constexpr int32_t chunk_size = 64;                       // voxels per side (8 bricks)

        [[=rh::edit, =rh::read_only]] float voxel_size = 0.1f;          // m
        [[=rh::edit, =rh::read_only]] int32_t fill_thickness = 3;       // voxels (voxel::VoxelizeSettings)
        [[=rh::edit, =rh::read_only]] uint32_t texels_per_voxel = 4;    // of the color atlas

        [[=rh::edit, =rh::read_only, =rh::transient]] VoxelCopyState state = VoxelCopyState::waiting;
        [[=rh::edit, =rh::read_only, =rh::transient]] uint32_t bricks = 0;
        [[=rh::edit, =rh::read_only, =rh::transient]] uint64_t solid_voxels = 0;
        [[=rh::edit, =rh::read_only, =rh::transient]] uint64_t solid_voxels_initial = 0;
        [[=rh::edit, =rh::read_only, =rh::transient]] uint32_t quads = 0;
        [[=rh::edit, =rh::read_only, =rh::transient]] uint32_t hits = 0;     // recompressions applied
        [[=rh::transient]] uint32_t hits_queued = 0;                          // ever: the index in the seeds
        [[=rh::edit, =rh::read_only, =rh::transient]] uint32_t chunk_count = 0;
        [[=rh::transient]] std::vector<VoxelChunk> chunks;
        [[=rh::transient]] std::shared_ptr<VoxelCopyBuild> build;
        // queued for the next job (in order)
        [[=rh::transient]] std::vector<VoxelHit> pending_hits;
        // the CPU state of the voxel copy (see above)
        [[=rh::transient]] std::shared_ptr<voxel::VoxelGrid> grid;
    };

    // System set of the Late system that builds and updates the voxel copies (before voxelize_on_impact)
    struct VoxelCopies {};

    // A VoxelDestructible turned into its voxel copy (voxelize_on_impact)
    struct VoxelizedEvent
    {
        ecs::Entity entity;
        glm::vec3 position{ 0.0f };     // of the impact
        std::string weapon;             // empty: the console command
    };
}
