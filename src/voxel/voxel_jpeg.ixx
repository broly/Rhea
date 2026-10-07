export module voxel:jpeg;

import std.compat;
import glm;

import :grid;

// 3D JPEG on voxels (V4): what a hit does to a voxel house. The voxels are cut into blocks of 8 x 8 x 8 (not
// the bricks: the block grid is shifted by 1..7 voxels per axis every generation, or recompressing converges
// and the artifacts stop growing) and every block whose center is within the radius goes through a real JPEG
// in 3D:
//
//     [downscale 2^3: each 2 x 2 x 2 cell its mean]  ->  RGB -> YCbCr (JFIF)  ->  [chroma 4:2:0: Cb, Cr per 2^3]
//     ->  3D DCT of Y, Cb, Cr and density  ->  quantization (Annex K tables made 3D, libjpeg's quality scale)
//     ->  inverse DCT  ->  RGB
//
// Colors turn blocky and ring, the density rings too: past the solid threshold that is holes in the walls and
// pieces floating next to them, the JPEG artifacts of the shape. Voxels without density first take the color of
// the nearest voxel with one (the surface pulled out along its normal, as a JPEG pads its edge blocks): the
// block then holds the surface's 2D pattern and rings like a 2D JPEG, where a flat fill would leave one flat
// color per block. A density left below `erase_below` becomes 0. Optionally a sharpen of the colors last.
// Optionally the blast also punches dithered holes, denser at its center (VoxelJpegSettings::holes).
// EXPERIMENT: the shape can instead be keyed by black, the empty voxels coded as black colors (jpeg_shape_mode
// below, chosen at compile time).
//
// Integer arithmetic only (a fixed point DCT, no floating point, no std::cos): the same grid, hit and seed give
// the same voxels on every machine. That is what the network sends (the hit, not the voxels).
export namespace voxel
{
    // ================================================================================================================
    // EXPERIMENT: how a hit decides the shape. Switch jpeg_shape_mode and rebuild (presets: assets/jpeg/presets.json
    // section "voxels").
    //
    //   density          the density is a channel of its own (density_quality / _scale / _noise, erase_below); empty
    //                    voxels take the surface's colors before coding, so no black bleeds in
    //   black_key        no density channel: an empty voxel is black, coded with the colors like the background of a
    //                    JPEG with no alpha. After decoding the luma decides: a voxel darker than key_keep is gone, an
    //                    empty one brighter than key_appear appears (the ringing around walls grows voxels: 3D
    //                    "mosquitoes")
    //   black_key_erode  as black_key, but nothing appears: voxels can only be lost
    //
    // In both black modes the colors of the voxels are lifted to a luma of key_floor first, so that only emptiness
    // is black and an undamaged dark plank is not keyed out by its own color.
    // ================================================================================================================
    enum class JpegShapeMode : uint8_t
    {
        density,
        black_key,
        black_key_erode,
    };
    inline constexpr JpegShapeMode jpeg_shape_mode = JpegShapeMode::density;

    enum class JpegChroma : uint8_t
    {
        full,           // 4:4:4
        half,           // 4:2:0 in 3D: one Cb, Cr per 2 x 2 x 2 voxels
    };

    struct VoxelJpegSettings
    {
        int32_t quality = 30;           // 1..100 of the colors (libjpeg scale)
        int32_t density_quality = 40;   // 1..100 of the density (luma table)
        // % of the density table's AC steps: a flat wall puts its energy into a few large coefficients and
        // survives even quality 1 (the error per voxel is about step / 45); the steps have to be larger than the
        // colors' for the shape to break up. The DC step stays (a coarse one moves the level of a whole block:
        // thin walls vanish or blocks fill up)
        int32_t density_scale = 100;
        // +- this much hashed noise (voxel and seed) on the density before coding: flat walls break up into
        // ragged holes instead of whole layers at once
        int32_t density_noise = 0;
        JpegChroma chroma = JpegChroma::half;
        int32_t downscale = 1;          // 1 or 2 (2 x 2 x 2 voxels merged first)
        int32_t generations = 1;        // recompressions of a hit, the grid shifted between them
        // % of an unsharp mask of the colors after the generations (6 neighbours with a color, the blocks
        // recompressed): brings out the block edges and the ringing, as the 2D chain's sharpen
        int32_t sharpen = 0;
        uint8_t erase_below = 24;       // densities below this become 0 (dust is not kept)

        // the black key shape modes (jpeg_shape_mode), luma 0..255:
        int32_t key_floor = 24;         // the voxels' colors are lifted to this luma before coding
        int32_t key_keep = 14;          // a voxel decoded darker than this is gone (below key_floor: undamaged
                                        // voxels stay)
        int32_t key_appear = 48;        // black_key: an empty voxel decoded brighter than this appears

        // Dithered holes: every voxel within the radius is emptied with a probability of
        //     holes % x (1 - (distance / radius)^2) ^ holes_falloff
        // by hashed noise (voxel and seed): dense at the center of the blast, sparse at its edge. holes_before:
        // punched before the codec (the empty voxels go in black / without density and get smeared, keyed, rung
        // with the rest), else after it (sharp holes in the result)
        int32_t holes = 0;              // % at the center, 0: none
        int32_t holes_falloff = 2;      // 1..8
        bool holes_before = true;
    };

    struct VoxelJpegStats
    {
        uint32_t blocks = 0;            // recompressed (with something in them)
        uint64_t solid_before = 0;      // solid voxels of those blocks
        uint64_t solid_after = 0;
        uint64_t holes = 0;             // voxels emptied by the dithered holes
        GridBounds changed;             // voxels that may have changed (all blocks recompressed)
        double milliseconds = 0.0;
    };

    // Recompresses the blocks whose center is within `radius` voxels of `center` (grid voxel coordinates:
    // integers, so a hit sent over the network selects the same blocks everywhere), settings.generations times.
    // `seed` picks the grid shifts: derive it from the hit. Increments the generation of the bricks touched,
    // flags them dirty, removes the bricks left empty.
    VoxelJpegStats recompress(VoxelGrid& grid, glm::ivec3 center, int32_t radius, const VoxelJpegSettings& settings,
        uint32_t seed);

    // The block grid's shift of a generation (1..7 per axis)
    glm::ivec3 jpeg_grid_shift(uint32_t seed, int32_t generation);

    // ---- the codec of one block (exposed for the tests) ----

    // 3D quantization table (512 entries, index x + 8 y + 64 z) of a quality 1..100
    std::array<int32_t, 512> quantization_table(int32_t quality, bool chroma);
    // values 0..255 in local_index order -> coded and decoded, in place
    void jpeg_block(std::array<int32_t, 512>& values, const std::array<int32_t, 512>& table);
}
