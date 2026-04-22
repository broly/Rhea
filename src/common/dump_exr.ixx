export module dump_exr;
import <array>;
import <vector>;
import <string>;
import <future>;
import <filesystem>;
import <json/value.h>;
import tinyexr;
#include "assertion_macros.h"

#define TINYEXR_SUCCESS (0)

export namespace exr_dump
{
    void save_exr(
        const std::filesystem::path& filename,
        const float* data,
        uint32_t width,
        uint32_t height,
        uint32_t channels = 4)
    {
        const char* err = nullptr;

        int ret = tinyexr::SaveEXR(
            data,
            (int)width,
            (int)height,
            (int)channels,
            /* save_as_fp16 = */ 0,
            filename.string().c_str(),
            &err
        );

        if (ret != TINYEXR_SUCCESS)
        {
            std::string msg = err ? err : "unknown error";
            FreeEXRErrorMessage(err);
            checkf(false, "EXR save failed: %s", msg.c_str());
        }
    }
}