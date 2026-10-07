export module voxel:voxelize;

import std.compat;
import glm;

import :grid;

// Mesh -> voxel grid (V2): what a voxel house is built from, once, then cached (VoxelCache).
//
//   surface   conservative: every voxel a triangle touches (triangle / box overlap) becomes solid, so a wall
//             thinner than a voxel still separates inside from outside. Its color is the base color where the
//             triangle closest to the voxel's center is nearest to it, read from the texture's mip level whose
//             texels are at most a voxel in size there (rounded down: an average over the voxel that keeps the
//             contrast, not one noisy texel)
//   fill      the empty voxels the outside cannot reach (6-connected flood fill from the border) are filled when
//             their region is thin: no voxel of it farther than fill_thickness voxels from a surface voxel. The
//             gaps between the planks and the inner layer of a wall become solid, closed rooms stay empty.
//             A filled voxel takes the color of its nearest surface voxel.
//
// The grid's origin is the mesh's bounds minimum snapped down to the voxel size, minus two voxels: voxel (0, 0, 0)
// is empty, coordinates are >= 0. Single threaded and deterministic for the same input and build.
export namespace voxel
{
    // RGBA8 image in the order of glTF / stb: row 0 is v = 0. Texel r | g << 8 | b << 16 | a << 24, sRGB
    struct Image
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint32_t> texels;
    };

    struct VoxelizeMaterial
    {
        const Image* base_color = nullptr;     // none: `color` alone
        glm::vec4 color{ 1.0f };               // multiplies the texture (sRGB, 0..1); alpha unused
        bool skip = false;                     // not voxelized (glass)
    };

    struct VoxelizePrimitive
    {
        std::span<const glm::vec3> positions;   // object local space (m)
        std::span<const glm::vec2> uvs;         // empty: no texture coordinates
        std::span<const uint32_t> indices;      // triangle list
        uint32_t material = 0;                  // index into the materials
    };

    struct VoxelizeSettings
    {
        float voxel_size = 0.1f;                // m
        int32_t fill_thickness = 3;             // voxels, 0: no fill
        uint64_t max_voxels = 64ull << 20;      // of the dense working box: larger meshes fail
    };

    struct VoxelizeStats
    {
        uint64_t triangles = 0;
        uint64_t surface_voxels = 0;
        uint64_t filled_voxels = 0;
        glm::ivec3 size{ 0 };                   // of the working box (voxels)
        double milliseconds = 0.0;
    };

    // nullopt with the reason in error (when given): nothing to voxelize, a box over max_voxels
    std::optional<VoxelGrid> voxelize(std::span<const VoxelizePrimitive> primitives,
        std::span<const VoxelizeMaterial> materials, const VoxelizeSettings& settings,
        VoxelizeStats* stats = nullptr, std::string* error = nullptr);

    // Conservative triangle / axis aligned box test (separating axes): true when they share any point
    bool triangle_overlaps_box(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 box_center, glm::vec3 box_half);
    // Point of the triangle closest to p as barycentric weights of a, b, c
    glm::vec3 closest_point_barycentric(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c);
}
