module assets:cubemap;

import paths;
import tinyexr;
import :asset_manager;
import <filesystem>;

#include "common/assertion_macros.h"


void Cubemap::save(const std::filesystem::path& filename) const
{
    checkf(face_size > 0, "Invalid cubemap face size");

    const uint32_t channels = 4;
    const uint32_t width  = face_size;
    const uint32_t height = face_size * 6;

    const size_t face_pixels = face_size * face_size * channels;

    // Validate faces
    for (int i = 0; i < 6; ++i)
    {
        checkf(faces[i].size() == face_pixels,
               "Cubemap face %d size mismatch", i);
    }

    // Interleave faces vertically
    std::vector<float> exr_pixels(width * height * channels);

    for (int face = 0; face < 6; ++face)
    {
        const float* src = faces[face].data();

        for (uint32_t y = 0; y < face_size; ++y)
        {
            const size_t dst_row =
                (face * face_size + y) * width * channels;

            const size_t src_row =
                y * width * channels;

            memcpy(
                &exr_pixels[dst_row],
                &src[src_row],
                width * channels * sizeof(float)
            );
        }
    }

    const char* err = nullptr;
    int ret = tinyexr::SaveEXR(
        exr_pixels.data(),
        static_cast<int>(width),
        static_cast<int>(height),
        channels,
        /* save_as_fp16 = */ 0,
        filename.string().c_str(),
        &err
    );

    checkf(ret == 0,
           "Failed to save cubemap EXR: %s",
           err ? err : "unknown error");
}

std::optional<Cubemap> Cubemap::load(const std::filesystem::path& filename)
{
    if (!std::filesystem::exists(filename))
        return std::nullopt;
    
    EXRVersion version;
    EXRHeader header;
    EXRImage image;

    InitEXRHeader(&header);
    InitEXRImage(&image);

    const char* err = nullptr;

    int ret = ParseEXRVersionFromFile(&version, filename.string().c_str());
    checkf(ret == 0, "Invalid EXR version");

    ret = ParseEXRHeaderFromFile(
        &header, &version,
        filename.string().c_str(),
        &err
    );
    checkf(ret == 0, err);

    // Request float32
    for (int i = 0; i < header.num_channels; i++)
        header.requested_pixel_types[i] = 2;  // TINYEXR_PIXELTYPE_FLOAT

    ret = LoadEXRImageFromFile(
        &image, &header,
        filename.string().c_str(),
        &err
    );
    checkf(ret == 0, err);

    checkf(image.num_channels >= 3,
           "Cubemap EXR must have at least RGB");

    const uint32_t width  = image.width;
    const uint32_t height = image.height;

    checkf(height % 6 == 0,
           "EXR height is not divisible by 6");

    const uint32_t face_size = height / 6;
    const uint32_t channels = 4;

    Cubemap cubemap;
    cubemap.format = TextureFormat::RGBA32F;
    cubemap.face_size = face_size;

    for (int i = 0; i < 6; ++i)
        cubemap.faces[i].resize(face_size * face_size * channels);

    // Map channels
    const float* chR = nullptr;
    const float* chG = nullptr;
    const float* chB = nullptr;
    const float* chA = nullptr;

    for (int c = 0; c < header.num_channels; c++)
    {
        const char* name = header.channels[c].name;
        const float* data =
            reinterpret_cast<float*>(image.images[c]);

        if (!strcmp(name, "R")) chR = data;
        else if (!strcmp(name, "G")) chG = data;
        else if (!strcmp(name, "B")) chB = data;
        else if (!strcmp(name, "A")) chA = data;
    }

    checkf(chR && chG && chB,
           "Missing RGB channels in cubemap EXR");

    // Deinterleave faces
    for (int face = 0; face < 6; ++face)
    {
        float* dst = cubemap.faces[face].data();

        for (uint32_t y = 0; y < face_size; ++y)
        {
            const uint32_t src_y = face * face_size + y;

            for (uint32_t x = 0; x < width; ++x)
            {
                const size_t src_idx = src_y * width + x;
                const size_t dst_idx =
                    (y * width + x) * channels;

                dst[dst_idx + 0] = chR[src_idx];
                dst[dst_idx + 1] = chG[src_idx];
                dst[dst_idx + 2] = chB[src_idx];
                dst[dst_idx + 3] = chA ? chA[src_idx] : 1.0f;
            }
        }
    }

    FreeEXRImage(&image);
    FreeEXRHeader(&header);

    return cubemap;
}

const Cubemap& CubemapHandle::get() const
{
    return AssetManager::get().get_cubemap(*this);
}

void serialize_json_value(CubemapHandle& target, const Json::Value& value, DependencyCollector* dc)
{
    if (!value.isString())
        return;

    std::string path = value.asString();
    target.pending_path = path;
    
    auto cubemap_future = AssetManager::get().load_cubemap_async(path);
    
    dc->push(std::async(
        std::launch::async,
        [&target, cubemap_future]() mutable
        {
            target = cubemap_future.get();
        }));
}
