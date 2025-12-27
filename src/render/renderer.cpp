#include "renderer.h"

#include "render_backend.h"

void Renderer::init(RBWindowHandle in_window, std::shared_ptr<World> in_world)
{
    world = in_world;
}

void Renderer::update_material_descriptor(const RenderMaterial& rm, const MaterialKey& key)
{
    render_backend->update_uniform_buffer(
        rm.material_ubo,
        MaterialUBO{
            .base_color = key.base_color,
            .metallic   = key.metallic,
            .roughness  = key.roughness
        }
    );

    render_backend->update_sampled_image(
        rm.layout,
        1, // albedo
        get_texture(key.albedo),
        ResourceUsageType::Persistent
    );

    render_backend->update_sampled_image(
        rm.layout,
        2, // normal
        get_texture(key.normal),
        ResourceUsageType::Persistent
    );
}

RBImageHandle Renderer::create_texture_from_asset(TextureHandle handle)
{
    assert(false);
    // const Texture& data = handle.get();
    //
    // RBImageHandle image = render_backend->create_texture_2d(
    //     data,
    //     TextureFormat::RGBA8
    // );
    //
    // texture_cache.emplace(handle, image);
    // return image;
    return {};
}

RBImageHandle Renderer::get_texture(TextureHandle handle)
{
    auto it = texture_cache.find(handle);
    if (it != texture_cache.end())
        return it->second;


    return create_texture_from_asset(handle);
}
