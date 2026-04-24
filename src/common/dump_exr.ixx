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

    void save_exr_padded(
        const std::filesystem::path& filename,
        const float* src_data,
        uint32_t width,
        uint32_t height,
        uint32_t src_channels,
        uint32_t out_channels,
        float placeholder_value = 0.0f,
        float alpha_value = 1.0f)
    {
        checkf(src_channels > 0, "src_channels must be > 0");
        checkf(out_channels > 0, "out_channels must be > 0");

        const uint32_t safe_out_channels =
            (out_channels == 2) ? 3u : out_channels;

        if (src_channels == safe_out_channels &&
            (safe_out_channels == 1 || safe_out_channels == 3 || safe_out_channels == 4))
        {
            save_exr(filename, src_data, width, height, safe_out_channels);
            return;
        }

        const size_t pixel_count = size_t(width) * height;
        std::vector<float> padded(pixel_count * safe_out_channels, placeholder_value);

        const uint32_t copy_channels = std::min(src_channels, safe_out_channels);
        const bool has_alpha_slot = (safe_out_channels == 4);

        for (size_t px = 0; px < pixel_count; ++px)
        {
            const float* src_px = src_data + px * src_channels;
            float* dst_px = padded.data() + px * safe_out_channels;

            for (uint32_t c = 0; c < copy_channels; ++c)
                dst_px[c] = src_px[c];

            if (has_alpha_slot && src_channels < 4)
                dst_px[3] = alpha_value;
        }

        save_exr(filename, padded.data(), width, height, safe_out_channels);
    }
}
