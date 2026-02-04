module assets:exr;

import paths;
import tinyexr;
#include "common/assertion_macros.h"


bool exr::save(const std::vector<float>& buffer,
               TextureFormat fmt,
               Extent dim,
               const std::string& path)
{
    auto filename = paths::get_cache_path() / path;

    if (fmt != TextureFormat::RGBA16F &&
        fmt != TextureFormat::RGBA32F)
    {
        return false;
    }

    const size_t expected = dim.width * dim.height * 4;
    checkf(buffer.size() == expected,
           "EXR save: buffer size mismatch");

    const char* err = nullptr;

    int ret = tinyexr::SaveEXR(
        buffer.data(),               // float*
        static_cast<int>(dim.width),
        static_cast<int>(dim.height),
        4,                            // RGBA
        /* save_as_fp16 = */ 0,       // 0 = float32
        filename.string().c_str(),
        &err
    );

    if (ret != 0)
    {
        checkf(false, "unable to save exr: %s", err ? err : "unknown error");
        return false;
    }

    return true;
}

