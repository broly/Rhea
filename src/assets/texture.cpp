module;

#include <json/value.h>

module assets;

import :texture;

import std.compat;
import assertions;

import stb_image;
import rhobject;
import cvar;
import globals;
import engine;
import :asset_manager;

#include "common/assertion_macros.h"

const Texture& TextureHandle::get() const
{
    return AssetManager::get().get_texture(*this);
}

std::shared_future<void> TextureHandle::resolve_async()
{
    checkf(pending_path.has_value(), "pending path is required");
    auto fut = AssetManager::get().load_texture_async(*pending_path);

    auto assign_id = std::async(std::launch::async, [this, fut]() {
        auto resolved = fut.get();
        this->id = resolved.id;
    }).share();

    return assign_id;
}

std::optional<Texture> Texture::create_from_file(const std::filesystem::path& path)
{
    Texture texture;
    
    checkf(path.extension() == ".png", "could not load non .png files yet");
    
    int width, height, channels;
    unsigned char* pixels = stb::load(path.string().c_str(),
                                      &width,
                                      &height,
                                      &channels,
                                      stb::STBI_rgb_alpha);

    if (!pixels)
    {
        return std::nullopt;
        // throw std::runtime_error("Failed to load texture: " + path.string());
    }

    texture.extent.width = static_cast<uint32_t>(width);
    texture.extent.height = static_cast<uint32_t>(height);
    texture.format = TextureFormat::RGBA8;

    size_t image_size = width * height * 4;
    texture.bulk.resize(image_size);
    memcpy(texture.bulk.data(), pixels, image_size);

    stb::free(pixels);

    return texture;
}

// Uncompressed RGBA8 with mips: a 4096^2 texture takes 85 MB of video memory, the Sponza materials alone ~6 GB
cvar::Var<int> cv_texture_max_size(
    "render.textures.max_size", 2048, "Material textures larger than this many texels per side are halved when loaded until they fit (0: no limit)",
    { .has_range = true, .min = 0.0f, .max = 16384.0f });

uint32_t get_texture_max_size()
{
    return uint32_t(std::max(cv_texture_max_size.get(), 0));
}

bool Texture::shrink_to_fit(uint32_t max_size)
{
    const bool rgba8 = format == TextureFormat::RGBA8 || format == TextureFormat::RGBA8_UNORM || format == TextureFormat::RGBA8_SRGB;
    if (max_size == 0 || std::max(extent.width, extent.height) <= max_size || !rgba8
        || bulk.size() != size_t(extent.width) * extent.height * 4)
        return false;

    // in place: texel (x, y) of the half is written before any read of the texels at or after (2x, 2y)
    auto* texels = reinterpret_cast<uint8_t*>(bulk.data());
    while (std::max(extent.width, extent.height) > max_size && extent.width >= 2 && extent.height >= 2)
    {
        const Extent half(extent.width / 2, extent.height / 2);
        for (uint32_t y = 0; y < half.height; ++y)
            for (uint32_t x = 0; x < half.width; ++x)
                for (uint32_t c = 0; c < 4; ++c)
                {
                    const size_t row0 = (size_t(2 * y) * extent.width + 2 * x) * 4 + c;
                    const size_t row1 = row0 + size_t(extent.width) * 4;
                    texels[(size_t(y) * half.width + x) * 4 + c] =
                        uint8_t((uint32_t(texels[row0]) + texels[row0 + 4] + texels[row1] + texels[row1 + 4] + 2) / 4);
                }
        extent = half;
    }
    bulk.resize(size_t(extent.width) * extent.height * 4);
    bulk.shrink_to_fit();
    return true;
}

bool Texture::save_to_file(const std::filesystem::path& path) const
{
    checkf(format == TextureFormat::RGBA8, "Only RGBA8 textures are supported");
    checkf(!bulk.empty(), "Texture bulk data is empty");
    checkf(path.extension() == ".png", "Only .png saving is supported");

    const int width  = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    const int channels = 4; // RGBA
    const int stride = width * channels;

    int result = stb::write_png(
        path.string().c_str(),
        width,
        height,
        channels,
        bulk.data(),
        stride
    );

    return result != 0;
}

void serialize_json_value(TextureHandle& target, const Json::Value& value, const SerializationContext& context)
{
    if (!value.isString())
        return;

    std::string path = value.asString();
    target.pending_path = path;
    
    auto texture_future = AssetManager::get().load_texture_async(path);
    
    context.dc->push(std::async(
        std::launch::async,
        [&target, texture_future]() mutable
        {
            target = texture_future.get();
        }));
}
