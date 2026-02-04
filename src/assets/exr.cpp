module assets:exr;

import paths;
import tinyexr;
#include "common/assertion_macros.h"

static float half_to_float(uint16_t h)
{
    const uint32_t sign = (h & 0x8000u) << 16;
    uint32_t mantissa = h & 0x03FFu;
    uint32_t exp = h & 0x7C00u;

    if (exp == 0x7C00u) // Inf / NaN
    {
        exp = 0x3FC00u;
    }
    else if (exp != 0)
    {
        exp += 0x1C000u;
    }
    else if (mantissa != 0)
    {
        exp = 0x1C400u;
        do
        {
            mantissa <<= 1;
            exp -= 0x400u;
        } while ((mantissa & 0x400u) == 0);
        mantissa &= 0x3FFu;
    }

    uint32_t f = sign | ((exp | mantissa) << 13);
    float result;
    memcpy(&result, &f, sizeof(float));
    return result;
}

bool exr::save(const std::vector<std::byte>& buffer, TextureFormat fmt, Dim2d dim, const std::string& path)
{
    auto filename = paths::get_cache_path() / path;
    
    bool flip_y = false;
    
    if (fmt != TextureFormat::RGBA16F)
        return false;

    const size_t pixel_count = dim.width * dim.height;
    const uint16_t* src = reinterpret_cast<const uint16_t*>(buffer.data());

    // RGBA float buffer for tinyexr
    std::vector<float> rgba(pixel_count * 4);

    for (uint32_t y = 0; y < dim.height; ++y)
    {
        uint32_t src_y = flip_y ? (dim.height - 1 - y) : y;

        for (uint32_t x = 0; x < dim.width; ++x)
        {
            size_t src_idx = (src_y * dim.width + x) * 4;
            size_t dst_idx = (y * dim.width + x) * 4;

            rgba[dst_idx + 0] = half_to_float(src[src_idx + 0]); // R
            rgba[dst_idx + 1] = half_to_float(src[src_idx + 1]); // G
            rgba[dst_idx + 2] = half_to_float(src[src_idx + 2]); // B
            rgba[dst_idx + 3] = half_to_float(src[src_idx + 3]); // A
        }
    }

    const char* err = nullptr;

    int ret = tinyexr::SaveEXR(
        rgba.data(),
        dim.width,
        dim.height,
        4,      // RGBA
        1,      // save as half
        filename.string().c_str(),
        &err
    );

    checkf(ret == 0, "unable to save exr: %s", err);

    return true;
}
