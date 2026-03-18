module render:renderer;

import :render_backend;
import paths;
import :material_instance;
import <filesystem>;
import reflect;
import :render_graph;
import json_utils;
import enum_helpers;
import :material_manager;
#include "common/assertion_macros.h"
#include "profiling/profile.h"


void Renderer::init(RBWindowHandle in_window)
{
    load_schemas();
    
    load_resources();
    
    
    SerializationContext ctx;
    ctx.strict_checking_enabled = true;
    const char* mat_manager_object = "render/material_manager.json";
    material_manager = json_object::load_object<MaterialManager>(paths::get_assets_path() / mat_manager_object,
        ctx, shared_from_this());
    
    
    
    const char* engine_config_path = "render/renderer.json";
    
    auto value = json_utils::load_json_asset(engine_config_path);
    checkf(value.has_value(), "could not load render/renderer.json");
    
    Json::Value const* render_graph_class_value = value->find("graph_class");
    checkf(render_graph_class_value != nullptr, "graph_class section is empty");
    
    main_render_graph_name = render_graph_class_value->asString();
    
    main_render_graph = create_render_graph(main_render_graph_name, {});
}

void Renderer::execute()
{
    checkf(main_render_graph != nullptr, "RenderGraph not initialized. Please create and initialize RenderGraph in your GameRenderer::init");
    
    execute_graph(main_render_graph);
    
    if (main_render_graph_needs_rebuild)
    {
        main_render_graph->recompile();
        main_render_graph_needs_rebuild = false;
    }
    
    if (!rg_once_jobs.empty())
    {
        for (const auto& job : rg_once_jobs)
        {
            auto rg = aux_graphs[job.render_graph_name];
            execute_graph(rg, job.params, job.callback);
        }
        rg_once_jobs.clear();
    }
    
}

void Renderer::execute_graph(
    std::shared_ptr<RenderGraph>& rg, 
    const RenderGraphParameters& params, 
    RGPostRenderCallback callback)
{
    PROFILE("execute_graph");
    
    if (NVTX_Start)
        NVTX_Start();
    
    
    auto& backend = *render_backend;
    RBFrameHandle frame = backend.get_current_frame();

    backend.wait_for_frame(frame);

    backend.reset_frame_fence(frame);

    backend.flush_frame_garbage(frame);

    if (!backend.acquire_next_image(frame))
    {
        rg->rebuild_resources();
        return;
    }

    RBCommandList cmd = backend.begin_commands(frame);

    rg->execute(cmd, frame, params, callback);

    backend.end_commands(cmd);

    if (!backend.submit_frame(frame, cmd))
    {
        rg->rebuild_resources();
        return;
    }
    
    if (NVTX_Finish)
        NVTX_Finish();
    

    backend.advance_frame();

}

std::shared_ptr<RenderGraph> Renderer::create_render_graph(Name render_graph_name,
    const std::map<Name, bool>& parameters, std::optional<Name> aux_graph_name)
{
    
    auto reflection_info = reflect::find_object_reflection_info(render_graph_name);
    checkf(reflection_info != nullptr, "Could not find render graph class");
    
    auto result = reflection_info->instantiate<RenderGraph>();
    result->setup(render_backend, shared_from_this());
    result->init_resources(parameters);
    result->build_passes(parameters);
    result->compile();
    
    if (aux_graph_name.has_value())
        aux_graphs.insert({*aux_graph_name, result});
    
    return result;
}

void Renderer::trigger_aux_rg_once(Name aux_rg_name, const RenderGraphParameters& params, RGPostRenderCallback callback)
{
    rg_once_jobs.push_back({aux_rg_name, params, callback});
}

void Renderer::load_schemas()
{
    auto dir = paths::get_assets_path() / "render" / "schemas";
    
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            if (entry.path().extension() == ".json")
            {
                if (entry.path().string()[0] == '!')
                    continue;
                files.emplace_back(entry.path().string());
            }
        }
    }
    
    SerializationContext ctx;
    ctx.strict_checking_enabled = true;
    
    for (auto& file : files)
    {
        if (auto obj = json_object::load_object<MaterialModel>(file, ctx))
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
                if (entry.path().string()[0] == '!')
                    continue;
                files.emplace_back(entry.path().string());
            }
        }
    }
    
    SerializationContext ctx;
    ctx.strict_checking_enabled = true;
    
    for (auto& file : files)
    {
        if (auto obj = json_object::load_object<RenderResourceInfo>(file, ctx))
            resources_info.insert({obj->resource.name, obj});
    }
    
    uint32_t set_index = 0;
    
    for (const auto& [_, resource_info] : resources_info)
    {
        RenderResourceDesc desc;
        auto& res = resource_info->resource;
        desc.name = res.name;
        desc.set = res.set;
        desc.allowed_stages = res.allowed_stages;
        res.set_index = set_index++;
        desc.set_index = res.set_index;
        
        desc.usage = res.usage;
        
        uint32_t binding = 0;
        
        for (const auto& [param_name, variable] : res.parameters)
        {
            RenderResourceVariableDesc var_desc;
            var_desc.parameter = variable;
            
            uint32_t current_binding = variable.is_descriptor() ? binding++ : -1;
            
            if (variable.type == MaterialParamType::uniform)
            {
                const reflect::RuntimeReflectionInfo* ubo_info = reflect::find_runtime_info(*variable.ubo);
                checkf(ubo_info, "could not find runtime type %s, please add runtime reflection", variable.ubo->to_string().c_str());
                var_desc.size = ubo_info->size;
                var_desc.binding = current_binding;
            }
            else if (variable.type == MaterialParamType::sampler)
            {
                auto found_sampler = samplers.find(*variable.sampler);
                checkf(found_sampler != samplers.end(), "Could not find sampler named %s", 
                    variable.sampler->to_string().c_str());
                var_desc.sampler = found_sampler->second;
                var_desc.size = -1;
                var_desc.binding = current_binding;
            }
            else if (variable.type == MaterialParamType::image)
            {
                var_desc.size = -1;
                var_desc.binding = current_binding;
            }
            else if (variable.type == MaterialParamType::tlas)
            {
                var_desc.size = -1;
                var_desc.binding = current_binding;
            }
            else if (variable.type == MaterialParamType::ssbo)
            {
                var_desc.size = -1;
                var_desc.binding = current_binding;
            }
            else if (variable.type == MaterialParamType::definition) // skip definitions
            {
                continue;
            } else
            {
                continue;  // skip all everything
            }
            desc.variables.push_back(var_desc);
        }
        resources[desc.name] = render_backend->create_resource(desc);
    }
}

void Renderer::set_flag(Name name, bool value, bool needs_rebuild)
{
    main_render_graph->set_flag(name, value);
}

void Renderer::toggle_flag(Name name, bool needs_rebuild)
{
    main_render_graph->toggle_flag(name, needs_rebuild);
    main_render_graph_needs_rebuild = true;
}

RBImageHandle Renderer::create_texture_from_asset(TextureHandle handle, bool generate_mips, 
        RBImageLayout initial_layout,
        RBImageLayout final_layout)
{
    const Texture& data = handle.get();
    
    RBImageHandle image = render_backend->create_texture_2d(
        data,
        TextureCreationInfo{
            TextureFormat::RGBA8,
            generate_mips,
            true,
            initial_layout,
            final_layout,
        }
    );
    
    texture_cache.emplace(handle, image);
    return image;
}

RBImageHandle Renderer::create_cubemap_from_asset(CubemapHandle handle)
{
    const Cubemap& data = handle.get();
    
    RBImageHandle image = render_backend->create_texture_cubemap(
        data,
        TextureCreationInfo{
            TextureFormat::RGBA32F,
            false,
        }
    );
    
    cubemap_cache.emplace(handle, image);
    return image;
}

RBImageHandle Renderer::get_texture(TextureHandle handle)
{
    auto it = texture_cache.find(handle);
    if (it != texture_cache.end())
        return it->second;

    return create_texture_from_asset(handle);
}

RBImageHandle Renderer::get_cubemap(CubemapHandle handle)
{
    auto it = cubemap_cache.find(handle);
    if (it != cubemap_cache.end())
        return it->second;

    return create_cubemap_from_asset(handle);
}

std::shared_ptr<PipelineFamily> Renderer::query_pipeline_family(Name pass_name, const std::shared_ptr<MaterialModel>& model)
{
    PROFILE("Renderer::query_pipeline_family");
    if (material_pipeline_families.contains({pass_name, model}))
        return material_pipeline_families.at({pass_name, model});
    
    auto family = new_object<PipelineFamily>(pass_name, model, render_backend, shared_from_this());
    
    material_pipeline_families.insert({{pass_name, model}, family});
    
    return family;
}


RenderResource* Renderer::query_material_resource(std::shared_ptr<MaterialModel> model, Name pass_name)
{
    if (material_resources.contains({pass_name, model}))
        return material_resources.at({pass_name, model});
    
    const MatModel_PipelineVariant* pipeline_config = model->get_pipeline_config_by_pass(pass_name);
    checkf(pipeline_config, "MaterialModel has no pipeline config that related to pass %s", pass_name.to_string().c_str());

    
    checkf(model->material_resource != std::nullopt, "material resource should provide 'material_resource' field");
    const Name material_resource_name = *model->material_resource;
    
    auto material_resource_info = find_resource_info(material_resource_name);
    
    RenderResourceDesc desc{};
    desc.name = model->model_name;
    desc.usage = material_resource_info->resource.usage;
    desc.set = material_resource_info->resource.set;

    // UBOs
    for (const auto& [name, param] : material_resource_info->resource.parameters)
    {
        // if (param.type == MaterialParamType::uniform || param.type == MaterialParamType::sampler)
        //     if (!param.passes.contains(pass_name))
        //         continue;
        if (param.type == MaterialParamType::uniform)
        {
            const Name cpp_ubo_name = *param.ubo;
            auto info = reflect::find_runtime_info(cpp_ubo_name);
            checkf(info, "Could not find type");
            desc.variables.push_back(RenderResourceVariableDesc{
                .parameter = param,
                .size = info->size,
            });
        } else if (param.type == MaterialParamType::sampler)
        {
            desc.variables.push_back(RenderResourceVariableDesc{
                .parameter = param,
                .sampler = samplers[*param.sampler],
            });
        } else if (param.type == MaterialParamType::image)
        {
            desc.variables.push_back(RenderResourceVariableDesc{
                .parameter = param,
            });
        } else if (param.type == MaterialParamType::ssbo)
        {
            desc.variables.push_back(RenderResourceVariableDesc{
                .parameter = param,
            });
        } else if (param.type == MaterialParamType::tlas)
        {
            desc.variables.push_back(RenderResourceVariableDesc{
                .parameter = param,
            });
        }
    }
    
    auto resource =  find_resource(material_resource_name);
    
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

std::shared_ptr<RenderResourceInfo> Renderer::find_resource_info(Name resource_name) const
{
    return resources_info.at(resource_name);
}

std::shared_ptr<MaterialInstance> Renderer::query_material_instance(std::shared_ptr<Material> material,
                                                                    Name pass_name)
{
    auto it = material_instances.find({material, pass_name});
    if (it != material_instances.end())
        return it->second;

    auto ptr = std::static_pointer_cast<Renderer>(shared_from_this());
    
    auto model = find_model(material->model);
    
    auto pipeline_family = query_pipeline_family(pass_name, model);
    
    auto instance = std::make_shared<MaterialInstance>(material, shared_from_this(), pass_name, pipeline_family);

    material_instances.emplace(std::pair{material, pass_name}, instance);
    return instance;
}

std::shared_ptr<MaterialModel> Renderer::find_model(Name model_name) const
{
    PROFILE("Renderer::find_model");
    auto model_it = models.find(model_name);
    if (model_it == models.end())
        return nullptr;
    return model_it->second;
}

RBSampler Renderer::get_sampler(Name sampler_name) const
{
    return samplers.at(sampler_name);
}

bool Renderer::get_render_flag(Name flag) const
{
    checkf(main_render_graph != nullptr, "Graph is null");
    
    return main_render_graph->get_render_flag(flag);
}
