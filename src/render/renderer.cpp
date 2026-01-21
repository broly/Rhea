module render:renderer;

import :render_backend;
import paths;
import <filesystem>;
import reflect;
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

std::shared_ptr<PipelineFamily> Renderer::get_or_create_material_pipeline_family(Name pass_name, const std::shared_ptr<MaterialModel>& model)
{
    if (material_pipeline_families.contains({pass_name, model}))
        return material_pipeline_families.at({pass_name, model});
    
    auto family = std::make_shared<PipelineFamily>(pass_name, model, render_backend, shared_from_this());
    
    material_pipeline_families.insert({{pass_name, model}, family});
    
    return family;
}

RenderResource* Renderer::get_or_create_resource_from_model(std::shared_ptr<MaterialModel> model, Name pass_name)
{
    if (material_resources.contains({pass_name, model}))
        return material_resources.at({pass_name, model});
    
    const MatModel_Pass* pass = model->get_pass_info(pass_name);
    checkf(pass, "MaterialModel has no pass");

    RenderResourceDesc desc{};
    desc.name = model->model_name;
    desc.usage_type = ResourceUsageType::persistent;
    desc.sampler = samplers[model->sampler];

    // UBOs
    for (const auto& [ubo_name, ubo] : model->uniform_buffers)
    {
        auto info = reflect::find_runtime_info(ubo.type_name);
        checkf(info, "Could not find type");
        desc.variables.push_back(RenderResourceVariable{
            .name = ubo_name,
            .set = model->set,
            .binding = ubo.binding,
            .size = info->size,
        });
    }

    // Parameters
    for (const auto& [param_name, param] : model->parameters)
    {
        if (param.shader_parameter)
        {
            desc.variables.push_back(RenderResourceVariable{
                .name = *param.shader_parameter,
                .set = model->set,
                .binding = *param.binding,
            });
        }
    }
    
    auto resource =  render_backend->create_resource(desc);
    
    material_resources.insert({std::pair{pass_name, model}, resource});
    
    return resource;
}

RenderResource* Renderer::create_material_resource(const RenderResourceDesc& desc)
{
    todo("not implemented yet");
}
