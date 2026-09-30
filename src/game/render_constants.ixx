export module game:constants;

import std.compat;
import rhmath;

export namespace Constants
{
    // atlas of the directional light's shadow cascades: shadow_atlas_tiles^2 tiles
    constexpr Extent shadowmap_extent = {4096, 4096};
    constexpr uint32_t shadow_atlas_tiles = 2;
    constexpr float shadow_distance = 220.0f;          // m, the last cascade ends there (or at the far plane)
    constexpr float shadow_split_lambda = 0.8f;        // cascade splits: 0 uniform .. 1 logarithmic
    constexpr Extent ibl_extent = {512, 512};
    constexpr Extent ibl_prefilter_extent = {128, 128};
    constexpr Extent zero_extent = {0, 0};
}
