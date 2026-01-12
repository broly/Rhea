module assets:texture;

import stb_image;
import <json/value.h>;

import globals;
import engine;
import <future>;

#include "common/assertion_macros.h"

const Texture& TextureHandle::get() const
{
    return RhGlobals::engine->asset_manager->get_texture(*this);
}

std::shared_future<void> TextureHandle::resolve_async()
{
    checkf(pending_path.has_value(), "pending path is required");
    auto fut = AssetManager::get().load_texture_async(*pending_path);
    
    std::shared_future<void> result = std::async(std::launch::async,[this, fut]
    {
        auto resolved = fut.get();
        id = resolved.id;
    });
    
    return result;
}

std::optional<Texture> Texture::create_from_file(const std::filesystem::path& path)
{
    Texture texture;
    
    int width, height, channels;
    unsigned char* pixels = stb::load(path.string().c_str(), 
                                &width, 
                                &height, 
                                &channels, 
                                stb::STBI_rgb_alpha);
    
    if (!pixels) {
        return std::nullopt;
        // throw std::runtime_error("Failed to load texture: " + path.string());
    }
    
    texture.width = static_cast<uint32_t>(width);
    texture.height = static_cast<uint32_t>(height);
    texture.format = TextureFormat::RGBA8;
    
    size_t image_size = width * height * 4;
    texture.pixels.resize(image_size);
    memcpy(texture.pixels.data(), pixels, image_size);
    
    stb::free(pixels);
    
    return texture;
}

void serialize_json_value(TextureHandle& target, const Json::Value& value, DependencyCollector* dc)
{
    if (!value.isString())
        return;

    std::string path = value.asString();
    target.pending_path = path;
    
    auto texture_future =
        RhGlobals::engine
            ->asset_manager
            ->load_texture_async(path);
    
    dc->push(std::async(
        std::launch::async,
        [&target, texture_future]() mutable
        {
            target = texture_future.get();
        }));
}
