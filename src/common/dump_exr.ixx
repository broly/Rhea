export module dump_exr;
import <array>;
import <vector>;
import <string>;
import <future>;
import <filesystem>;
import <algorithm>;
import <cmath>;
import <cstdint>;
import <json/value.h>;
import tinyexr;
#include "assertion_macros.h"

#include "stb_image_write.h"

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

    namespace detail
    {
        inline float linear_to_srgb(float x)
        {
            if (!std::isfinite(x)) return 0.0f;
            x = std::max(0.0f, x);
            return (x <= 0.0031308f)
                ? 12.92f * x
                : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
        }

        inline uint8_t float_to_u8(float x)
        {
            const float v = std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f;
            return static_cast<uint8_t>(v);
        }

        std::vector<float> downscale_box(
            const float* src,
            uint32_t src_w,
            uint32_t src_h,
            uint32_t channels,
            uint32_t dst_w,
            uint32_t dst_h)
        {
            std::vector<float> dst(size_t(dst_w) * dst_h * channels, 0.0f);

            const float sx = float(src_w) / float(dst_w);
            const float sy = float(src_h) / float(dst_h);

            for (uint32_t y = 0; y < dst_h; ++y)
            {
                const uint32_t y0 = uint32_t(std::floor(y * sy));
                const uint32_t y1 = std::max(y0 + 1u,
                                             uint32_t(std::floor((y + 1) * sy)));
                const uint32_t y1c = std::min(y1, src_h);

                for (uint32_t x = 0; x < dst_w; ++x)
                {
                    const uint32_t x0 = uint32_t(std::floor(x * sx));
                    const uint32_t x1 = std::max(x0 + 1u,
                                                 uint32_t(std::floor((x + 1) * sx)));
                    const uint32_t x1c = std::min(x1, src_w);

                    float* out = dst.data() + (size_t(y) * dst_w + x) * channels;

                    for (uint32_t c = 0; c < channels; ++c)
                    {
                        double sum = 0.0;
                        uint32_t valid = 0;

                        for (uint32_t yy = y0; yy < y1c; ++yy)
                        {
                            const float* row = src + (size_t(yy) * src_w + x0) * channels;
                            for (uint32_t xx = x0; xx < x1c; ++xx, row += channels)
                            {
                                const float v = row[c];
                                if (std::isfinite(v))
                                {
                                    sum += v;
                                    ++valid;
                                }
                            }
                        }

                        out[c] = (valid > 0) ? float(sum / double(valid)) : 0.0f;
                    }
                }
            }

            return dst;
        }

        float compute_log_average_luminance(
            const float* data,
            size_t pixel_count,
            uint32_t src_channels)
        {
            constexpr float epsilon = 1e-4f;
            double log_sum = 0.0;
            size_t valid = 0;

            const bool has_rgb = (src_channels >= 3);

            for (size_t px = 0; px < pixel_count; ++px)
            {
                const float* p = data + px * src_channels;
                const float lum = has_rgb
                    ? (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2])
                    : p[0];

                if (!std::isfinite(lum) || lum <= 0.0f)
                    continue;

                log_sum += std::log(epsilon + lum);
                ++valid;
            }

            if (valid == 0)
                return 0.18f;

            return float(std::exp(log_sum / double(valid)));
        }
    }

    void save_png_preview(
        const std::filesystem::path& filename,
        const float* data,
        uint32_t width,
        uint32_t height,
        uint32_t src_channels,
        uint32_t resolution_percent = 50,
        float key = 0.18f,
        float white_point = 0.0f)
    {
        checkf(src_channels > 0, "src_channels must be > 0");
        checkf(resolution_percent > 0 && resolution_percent <= 100,
            "resolution_percent must be in (0, 100], got %u", resolution_percent);


        std::vector<float> resized_storage;
        const float* work_data = data;
        uint32_t work_w = width;
        uint32_t work_h = height;

        if (resolution_percent < 100)
        {
            const uint32_t dst_w = std::max(1u, (width  * resolution_percent) / 100u);
            const uint32_t dst_h = std::max(1u, (height * resolution_percent) / 100u);

            if (dst_w != width || dst_h != height)
            {
                resized_storage = detail::downscale_box(
                    data, width, height, src_channels, dst_w, dst_h);
                work_data = resized_storage.data();
                work_w = dst_w;
                work_h = dst_h;
            }
        }

        const size_t pixel_count = size_t(work_w) * work_h;
        const bool has_rgb = (src_channels >= 3);
        const bool has_alpha = (src_channels == 2 || src_channels == 4);

        const float avg_lum = detail::compute_log_average_luminance(
            work_data, pixel_count, src_channels);
        const float scale = key / std::max(avg_lum, 1e-6f);

        const float w2 = white_point * white_point;
        const bool use_white = (white_point > 0.0f);

        const int out_channels = has_alpha ? 4 : 3;
        std::vector<uint8_t> ldr(pixel_count * out_channels);

        for (size_t px = 0; px < pixel_count; ++px)
        {
            const float* src = work_data + px * src_channels;

            float r, g, b;
            if (has_rgb) { r = src[0]; g = src[1]; b = src[2]; }
            else         { r = g = b = src[0]; }

            if (!std::isfinite(r)) r = 0.0f;
            if (!std::isfinite(g)) g = 0.0f;
            if (!std::isfinite(b)) b = 0.0f;
            r = std::max(0.0f, r);
            g = std::max(0.0f, g);
            b = std::max(0.0f, b);

            r *= scale; g *= scale; b *= scale;

            auto tonemap = [&](float x) -> float
            {
                if (use_white)
                    return (x * (1.0f + x / w2)) / (1.0f + x);
                return x / (1.0f + x);
            };

            r = detail::linear_to_srgb(tonemap(r));
            g = detail::linear_to_srgb(tonemap(g));
            b = detail::linear_to_srgb(tonemap(b));

            uint8_t* dst = ldr.data() + px * out_channels;
            dst[0] = detail::float_to_u8(r);
            dst[1] = detail::float_to_u8(g);
            dst[2] = detail::float_to_u8(b);

            if (has_alpha)
            {
                const float a = (src_channels == 2) ? src[1] : src[3];
                dst[3] = detail::float_to_u8(std::isfinite(a) ? a : 1.0f);
            }
        }

        const int stride = int(work_w) * out_channels;
        const int ok = stbi_write_png(
            filename.string().c_str(),
            int(work_w),
            int(work_h),
            out_channels,
            ldr.data(),
            stride);

        checkf(ok != 0, "PNG preview save failed: %s", filename.string().c_str());
    }

    void save_exr_padded(
        const std::filesystem::path& filename,
        const float* src_data,
        uint32_t width,
        uint32_t height,
        uint32_t src_channels,
        uint32_t out_channels,
        float placeholder_value = 0.0f,
        float alpha_value = 1.0f,
        bool save_preview = true,
        uint32_t preview_resolution_percent = 50)
    {
        checkf(src_channels > 0, "src_channels must be > 0");
        checkf(out_channels > 0, "out_channels must be > 0");

        const uint32_t safe_out_channels =
            (out_channels == 2) ? 3u : out_channels;

        auto write_preview = [&]()
        {
            if (!save_preview) return;
            std::filesystem::path png_path = filename;
            png_path.replace_extension(".png");
            save_png_preview(
                png_path, src_data, width, height, src_channels,
                preview_resolution_percent);
        };

        if (src_channels == safe_out_channels &&
            (safe_out_channels == 1 || safe_out_channels == 3 || safe_out_channels == 4))
        {
            save_exr(filename, src_data, width, height, safe_out_channels);
            write_preview();
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
        write_preview();
    }
}