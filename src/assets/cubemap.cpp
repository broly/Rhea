module assets:cubemap;

import paths;
import tinyexr;
import :asset_manager;
import :asset;
import <filesystem>;

#include "common/assertion_macros.h"


void Cubemap::save(const std::filesystem::path& filename) const
{
    checkf(face_size > 0, "Invalid cubemap face size");

    constexpr uint32_t channels = 4;
    constexpr uint32_t face_count = 6;

    const uint32_t mip_count =
        static_cast<uint32_t>(faces[0].size());

    checkf(mip_count > 0, "Cubemap has no mips");

    // Validate layout
    for (uint32_t face = 0; face < face_count; ++face)
    {
        checkf(faces[face].size() == mip_count,
               "Cubemap face %u mip count mismatch", face);
    }

    auto mip_size = [](uint32_t base, uint32_t mip)
    {
        return std::max(1u, base >> mip);
    };

    // --------------------------------------------------
    // Calculate EXR dimensions
    // --------------------------------------------------

    uint32_t width  = face_size;
    uint32_t height = 0;

    for (uint32_t mip = 0; mip < mip_count; ++mip)
        height += mip_size(face_size, mip) * face_count;

    std::vector<float> exr_pixels(
        static_cast<size_t>(width) *
        static_cast<size_t>(height) *
        channels
    );

    // --------------------------------------------------
    // Pack faces + mips
    // --------------------------------------------------

    uint32_t y_offset = 0;

    for (uint32_t mip = 0; mip < mip_count; ++mip)
    {
        const uint32_t size = mip_size(face_size, mip);
        const size_t row_floats = size * channels;

        for (uint32_t face = 0; face < face_count; ++face)
        {
            const auto& src = faces[face][mip];

            checkf(src.size() ==
                   static_cast<size_t>(size) *
                   size * channels,
                   "Invalid cubemap face data");

            for (uint32_t y = 0; y < size; ++y)
            {
                const size_t dst_row =
                    (static_cast<size_t>(y_offset + y) *
                     width) * channels;

                const size_t src_row =
                    static_cast<size_t>(y) * row_floats;

                memcpy(
                    &exr_pixels[dst_row],
                    &src[src_row],
                    row_floats * sizeof(float)
                );
            }

            y_offset += size;
        }
    }

    // --------------------------------------------------
    // Save EXR
    // --------------------------------------------------

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


std::optional<Cubemap> Cubemap::load(
    const std::filesystem::path& filename)
{
    if (!std::filesystem::exists(filename))
        return std::nullopt;

    EXRVersion version;
    EXRHeader  header;
    EXRImage   image;

    InitEXRHeader(&header);
    InitEXRImage(&image);

    const char* err = nullptr;

    int ret = ParseEXRVersionFromFile(
        &version, filename.string().c_str());
    checkf(ret == 0, "Invalid EXR version");

    ret = ParseEXRHeaderFromFile(
        &header, &version,
        filename.string().c_str(),
        &err);
    checkf(ret == 0, err);

    // Force float32
    for (int i = 0; i < header.num_channels; ++i)
        header.requested_pixel_types[i] =
            2;  // TINYEXR_PIXELTYPE_FLOAT (2)

    ret = LoadEXRImageFromFile(
        &image, &header,
        filename.string().c_str(),
        &err);
    checkf(ret == 0, err);

    checkf(image.num_channels >= 3,
           "Cubemap EXR must have RGB");

    const uint32_t width  = image.width;
    const uint32_t height = image.height;

    // --------------------------------------------------
    // Map channels
    // --------------------------------------------------

    const float* chR = nullptr;
    const float* chG = nullptr;
    const float* chB = nullptr;
    const float* chA = nullptr;

    for (int c = 0; c < header.num_channels; ++c)
    {
        const char* name = header.channels[c].name;
        const float* data =
            reinterpret_cast<const float*>(image.images[c]);

        if (!strcmp(name, "R")) chR = data;
        else if (!strcmp(name, "G")) chG = data;
        else if (!strcmp(name, "B")) chB = data;
        else if (!strcmp(name, "A")) chA = data;
    }

    checkf(chR && chG && chB,
           "Missing RGB channels");

    // --------------------------------------------------
    // Deduce mip count
    // --------------------------------------------------

    auto mip_size = [](uint32_t base, uint32_t mip)
    {
        return std::max(1u, base >> mip);
    };

    uint32_t face_size = width;
    uint32_t mip_count = 0;
    uint32_t consumed_height = 0;

    while (consumed_height < height)
    {
        const uint32_t size = mip_size(face_size, mip_count);
        consumed_height += size * 6;
        ++mip_count;
    }

    checkf(consumed_height == height,
           "Invalid cubemap EXR height");

    // --------------------------------------------------
    // Allocate cubemap
    // --------------------------------------------------

    Cubemap cubemap;
    cubemap.format    = TextureFormat::RGBA32F;
    cubemap.face_size = face_size;
    cubemap.id = INVALID_ASSET_ID;

    for (uint32_t face = 0; face < 6; ++face)
        cubemap.faces[face].resize(mip_count);

    // --------------------------------------------------
    // Unpack faces + mips
    // --------------------------------------------------

    uint32_t y_offset = 0;

    for (uint32_t mip = 0; mip < mip_count; ++mip)
    {
        const uint32_t size = mip_size(face_size, mip);

        for (uint32_t face = 0; face < 6; ++face)
        {
            auto& dst = cubemap.faces[face][mip];
            dst.resize(size * size * 4);

            for (uint32_t y = 0; y < size; ++y)
            {
                const uint32_t src_y = y_offset + y;

                for (uint32_t x = 0; x < size; ++x)
                {
                    const size_t src_idx =
                        static_cast<size_t>(src_y) * width + x;

                    const size_t dst_idx =
                        (static_cast<size_t>(y) * size + x) * 4;

                    dst[dst_idx + 0] = chR[src_idx];
                    dst[dst_idx + 1] = chG[src_idx];
                    dst[dst_idx + 2] = chB[src_idx];
                    dst[dst_idx + 3] = chA ? chA[src_idx] : 1.0f;
                }
            }

            y_offset += size;
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

void serialize_json_value(CubemapHandle& target, const Json::Value& value, const SerializationContext& context)
{
    if (!value.isString())
        return;

    std::string path = value.asString();
    target.pending_path = path;
    
    auto cubemap_future = AssetManager::get().load_cubemap_async(path);
    
    context.dc->push(std::async(
        std::launch::async,
        [&target, cubemap_future]() mutable
        {
            target = cubemap_future.get();
        }));
}
