export module game:constants;

import rhmath;

export namespace Constants
{
    constexpr Extent shadowmap_extent = {4096, 4096};
    constexpr Extent ibl_extent = {512, 512};
    constexpr Extent ibl_prefilter_extent = {128, 128};
    constexpr Extent zero_extent = {0, 0};
}
