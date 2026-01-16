module render:renderer;

import :render_backend;
#include "common/assertion_macros.h"


void Renderer::init(RBWindowHandle in_window)
{
    // world = in_world;
}

void Renderer::update_material_resource(RenderResourceInstance* material_resource_instance, MaterialKey material_key, RBFrameHandle frame)
{
    unreachable("pure virtual");
}

RBImageHandle Renderer::create_texture_from_asset(TextureHandle handle)
{
    const Texture& data = handle.get();
    
    RBImageHandle image = render_backend->create_texture_2d(
        data,
        TextureFormat::RGBA8
    );
    
    texture_cache.emplace(handle, image);
    return image;
}

RBImageHandle Renderer::get_texture(TextureHandle handle)
{
    auto it = texture_cache.find(handle);
    if (it != texture_cache.end())
        return it->second;

    return create_texture_from_asset(handle);
}

RenderResource* Renderer::get_material_resource()
{
    unreachable("pure virtual");
}

RenderResource* Renderer::create_material_resource(const RenderResourceDesc& desc)
{
    todo("not implemented yet");
}
