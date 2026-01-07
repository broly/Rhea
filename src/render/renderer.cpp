module render:renderer;

import :render_backend;
import <cassert>;

#include "common/assertion_macros.h"

void Renderer::init(RBWindowHandle in_window)
{
    // world = in_world;
}

void Renderer::update_material_descriptor(const RenderMaterial& rm, const MaterialKey& key)
{
    assert(false);
}

RBDescriptorSet Renderer::allocate_material_descriptor()
{
    assert(false);
    return {};
}

RBBufferHandle Renderer::create_material_ubo()
{
    assert(false);
    return {};
}

RBDescriptorSetLayout Renderer::get_material_layout() const
{
    assert(false);
    return {};
}

void Renderer::bind_material_ubo(const RenderMaterial& rm)
{
    assert(false);
}

void Renderer::update_material_resource(RenderResourceInstance* material_resource_instance, MaterialKey material_key, RBFrameHandle frame)
{
    assert(false);
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
    unreachable("qweqwf");
}
