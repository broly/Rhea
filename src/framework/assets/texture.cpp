#include "texture.h"
#include <stb_image.h>
#include <json/value.h>

#include "engine.h"
#include "globals.h"

const Texture& TextureHandle::get() const
{
    return RhGlobals::engine->asset_manager->get_texture(*this);
}

Texture Texture::create_from_file(const std::filesystem::path& path)
{
    Texture texture;
    
    int width, height, channels;
    stbi_uc* pixels = stbi_load(path.string().c_str(), 
                                &width, 
                                &height, 
                                &channels, 
                                STBI_rgb_alpha);
    
    if (!pixels) {
        throw std::runtime_error("Failed to load texture: " + path.string());
    }
    
    texture.width = static_cast<uint32_t>(width);
    texture.height = static_cast<uint32_t>(height);
    texture.format = TextureFormat::RGBA8;
    
    size_t image_size = width * height * 4;
    texture.pixels.resize(image_size);
    memcpy(texture.pixels.data(), pixels, image_size);
    
    stbi_image_free(pixels);
    
    return texture;
}

void serialize_json_value(TextureHandle& target, const Json::Value& value)
{
    if (value.isString())
    {
        std::string path = value.asString();
        target = RhGlobals::engine->asset_manager->load_texture(path);
    }
}
