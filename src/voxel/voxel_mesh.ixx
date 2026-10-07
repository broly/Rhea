export module voxel:mesh;

import std.compat;
import glm;

import :grid;

// Voxel grid -> triangle mesh with a color atlas (V3, the CPU path): what the renderer draws as an ordinary
// mesh (shadows, probes, occlusion culling as for any other), rebuilt for the part that changed.
//
// A face is where a solid voxel meets a non-solid one. Faces of one direction in one slice are merged greedily
// into rectangles whatever their colors; every rectangle gets a patch of the atlas, texels_per_voxel texels per
// voxel side (each voxel a flat square: sharp voxel edges under linear filtering) with `padding` texels of its
// edge colors around it (mip levels do not bleed in its neighbours). Patches are packed in shelves into a
// power of two atlas; when it would exceed max_atlas_size the texels per voxel are halved.
//
// Positions are in the grid's (object) local space, triangles counter-clockwise seen from outside (glTF).
export namespace voxel
{
    struct SurfaceMeshSettings
    {
        uint32_t texels_per_voxel = 4;
        uint32_t padding = 2;
        uint32_t max_atlas_size = 4096;
        // both set: exactly this atlas (the texels per voxel shrink until the patches fit): a rebuilt mesh keeps
        // the size of its texture, which is then updated in place
        uint32_t atlas_width = 0;
        uint32_t atlas_height = 0;
    };

    struct SurfaceMesh
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec4> tangents;        // xyz along +u of the atlas, w = 1
        std::vector<glm::vec2> uvs;
        std::vector<uint32_t> indices;          // triangle list
        uint32_t quads = 0;

        // RGBA8 sRGB, r | g << 8 | b << 16 | a << 24, row 0 at v = 0
        uint32_t atlas_width = 0;
        uint32_t atlas_height = 0;
        uint32_t texels_per_voxel = 0;          // as built (may be below the setting)
        std::vector<uint32_t> atlas;

        bool empty() const { return indices.empty(); }
    };

    // Faces of the solid voxels in `voxels` (inclusive range); their neighbours are read from the grid also
    // outside the range, so meshes of neighbouring ranges fit together. nullopt with the reason in error when
    // the atlas does not fit even at one texel per voxel.
    std::optional<SurfaceMesh> build_surface_mesh(const VoxelGrid& grid, const GridBounds& voxels,
        const SurfaceMeshSettings& settings = {}, std::string* error = nullptr);

    // Whole grid
    std::optional<SurfaceMesh> build_surface_mesh(const VoxelGrid& grid, const SurfaceMeshSettings& settings = {},
        std::string* error = nullptr);
}
