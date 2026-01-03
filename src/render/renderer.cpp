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
    render_backend->update_uniform_buffer(
        rm.material_ubo,
        MaterialUBO{
            .base_color_mult = key.base_color_mult,
            .emissive_mult = key.emissive_mult,
            .occlusion_mult = key.occlusion_mult,
            .roughness_mult = key.roughness_mult,
            .metallic_mult = key.metallic_mult,
        }
    );

    render_backend->update_sampled_image(
        rm.layout,
        1, // base_color
        get_texture(key.base_color),
        ResourceUsageType::Persistent
    );

    render_backend->update_sampled_image(
        rm.layout,
        2, // emissive
        get_texture(key.emissive),
        ResourceUsageType::Persistent
    );

    render_backend->update_sampled_image(
        rm.layout,
        3, // normal
        get_texture(key.normal),
        ResourceUsageType::Persistent
        );

    render_backend->update_sampled_image(
        rm.layout,
        4, // occlusion_roughness_metallic
        get_texture(key.occlusion_roughness_metallic),
        ResourceUsageType::Persistent
    );
}

RBDescriptorSet Renderer::allocate_material_descriptor()
{
    auto result = render_backend->allocate_descriptor_sets_for_layout(
        material_layout,
        ResourceUsageType::Persistent
    );
    
    ensure(result.has_value());
    
    return *result;
}

RBBufferHandle Renderer::create_material_ubo()
{
    return render_backend->create_uniform_buffer(
        sizeof(MaterialUBO),
        ResourceUsageType::Persistent
    );
}

void Renderer::bind_material_ubo(const RenderMaterial& rm)
{
    render_backend->bind_buffer_to_descriptor(
        rm.layout,
        0,      // binding = 0
        rm.material_ubo
    );
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
