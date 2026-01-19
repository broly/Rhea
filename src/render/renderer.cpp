module render:renderer;

import :render_backend;
import paths;
import <filesystem>;
#include "common/assertion_macros.h"


void Renderer::init(RBWindowHandle in_window)
{
    load_schemas();
}

void Renderer::load_schemas()
{
    auto dir = paths::get_assets_path() / "render" / "schemas";
    
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            if (entry.path().extension() == ".json")
            {
                files.emplace_back(entry.path().string());
            }
        }
    }
    
    for (auto& file : files)
    {
        if (auto obj = json_object::load_object<MaterialModel>(file))
            models.insert({obj->model_name, obj});
    }
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

std::shared_ptr<PipelineFamily> Renderer::get_or_create_material_pipeline_family(Name model_name)
{
    todo("not implemented yet");
}

RenderResource* Renderer::get_material_resource()
{
    unreachable("pure virtual");
}

RenderResource* Renderer::create_material_resource(const RenderResourceDesc& desc)
{
    todo("not implemented yet");
}
