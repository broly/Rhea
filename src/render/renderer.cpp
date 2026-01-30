module render:renderer;

import :render_backend;
import paths;
import <filesystem>;
import reflect;
#include "common/assertion_macros.h"


void Renderer::init(RBWindowHandle in_window)
{
    load_schemas();
    
    load_resources();
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

void Renderer::load_resources()
{
    auto dir = paths::get_assets_path() / "render" / "resources";
    
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
        if (auto obj = json_object::load_object<RenderResourceInfo>(file))
            resources_info.insert({obj->name, obj});
    }
    
    for (const auto& [_, resource_info] : resources_info)
    {
        RenderResourceDesc desc;
        desc.name = resource_info->name;
        desc.set = resource_info->set;
        if (resource_info->sampler.has_value())
        {
            auto found_sampler = samplers.find(*resource_info->sampler);
            checkf(found_sampler != samplers.end(), "Could not find sampler named %s", 
                resource_info->sampler->to_string().c_str());
            desc.sampler = found_sampler->second;
        }
        desc.stages = ShaderStage::none;
        for (ShaderStage stage : resource_info->stages)
            desc.stages |= stage;
        
        desc.usage_type = resource_info->usage;
        for (const MatModel_Parameter& variable : resource_info->variables)
        {
            checkf(variable.type != MaterialParamType::definition, "Resource variable type could not be definition");
            RenderResourceVariableDesc var_desc;
            var_desc.name = *variable.variable;
            var_desc.binding = *variable.binding;
            var_desc.set = resource_info->set;
            if (variable.type == MaterialParamType::uniform)
            {
                const reflect::RuntimeReflectionInfo* ubo_info = reflect::find_runtime_info(*variable.ubo);
                checkf(ubo_info, "could not find runtime type %s, please add runtime reflection", variable.ubo->to_string().c_str());
                var_desc.size = ubo_info->size;
            }
            else if (variable.type == MaterialParamType::sampler)
            {
                var_desc.size = -1;
            }
            else
            {
                continue;
            }
            desc.variables.push_back(var_desc);
        }
        resources[desc.name] = render_backend->create_resource(desc);
    }
}

void Renderer::set_flag(Name name, bool value, bool needs_rebuild)
{
    render_flags[name] = value;
}

void Renderer::toggle_flag(Name name, bool needs_rebuild)
{
    render_flags[name] = !render_flags[name];
    render_graph_needs_rebuild = true;
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
    desc.usage_type = model->usage_type;
    desc.sampler = samplers[model->sampler];
    desc.set = model->set;

    // UBOs
    for (const auto& [name, param] : model->parameters)
    {
        if (param.type == MaterialParamType::uniform)
        {
            const Name cpp_ubo_name = *param.ubo;
            auto info = reflect::find_runtime_info(cpp_ubo_name);
            checkf(info, "Could not find type");
            desc.variables.push_back(RenderResourceVariableDesc{
                .name = *param.variable,
                .size = info->size,
            });
        } else if (param.type == MaterialParamType::sampler)
        {
            desc.variables.push_back(RenderResourceVariableDesc{
                .name = *param.variable
            });
        }
    }
    
    auto resource =  render_backend->create_resource(desc);
    
    material_resources.insert({std::pair{pass_name, model}, resource});
    
    return resource;
}


RenderResource* Renderer::find_resource(Name resource_name) const
{
    auto resource_it = resources.find(resource_name);
    if (resource_it == resources.end())
        return nullptr;
    
    return resource_it->second;
}
