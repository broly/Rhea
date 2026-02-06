module render:renderer;

import :render_backend;
import paths;
import :material_instance;
import <filesystem>;
import reflect;
import :render_graph;
import json_utils;
#include "common/assertion_macros.h"


void Renderer::init(RBWindowHandle in_window)
{
    load_schemas();
    
    load_resources();
    
    
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
    
    execute_graph(main_render_graph);
}

void Renderer::execute_graph(
    std::shared_ptr<RenderGraph>& rg, 
    const RenderGraphParameters& params, 
    RGPostRenderCallback callback)
{
    
    auto& backend = *render_backend;
    
    RBFrameHandle frame = backend.get_current_frame();

    backend.wait_for_frame(frame);

    backend.reset_frame_fence(frame);

    if (!backend.acquire_next_image(frame))
    {
        rg->rebuild_resources();
        return;
    }

    RBCommandList cmd = backend.begin_commands(frame);
    rg->execute(cmd, frame, params, callback);
    backend.end_commands(cmd);

    backend.submit_frame(frame, cmd);

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
    main_render_graph->set_flag(name, value);
}

void Renderer::toggle_flag(Name name, bool needs_rebuild)
{
    main_render_graph->toggle_flag(name, needs_rebuild);
    main_render_graph_needs_rebuild = true;
}

RBImageHandle Renderer::create_texture_from_asset(TextureHandle handle)
{
    const Texture& data = handle.get();
    
    RBImageHandle image = render_backend->create_texture_2d(
        data,
        TextureCreationInfo{
            TextureFormat::RGBA8,
            true,
        }
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

std::shared_ptr<PipelineFamily> Renderer::query_pipeline_family(Name pass_name, const std::shared_ptr<MaterialModel>& model)
{
    if (material_pipeline_families.contains({pass_name, model}))
        return material_pipeline_families.at({pass_name, model});
    
    auto family = std::make_shared<PipelineFamily>(pass_name, model, render_backend, shared_from_this());
    
    material_pipeline_families.insert({{pass_name, model}, family});
    
    return family;
}

RenderResource* Renderer::query_resource(std::shared_ptr<MaterialModel> model, Name pass_name)
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

std::shared_ptr<MaterialInstance> Renderer::query_material_instance(std::shared_ptr<Material> material,
    Name pass_name)
{
    auto it = material_instances.find({material, pass_name});
    if (it != material_instances.end())
        return it->second;

    auto ptr = std::static_pointer_cast<Renderer>(shared_from_this());
    
    auto instance = std::make_shared<MaterialInstance>(material, shared_from_this(), pass_name);

    material_instances.emplace(std::pair{material, pass_name}, instance);
    return instance;
}

std::shared_ptr<MaterialModel> Renderer::find_model(Name model_name) const
{
    auto model_it = models.find(model_name);
    if (model_it == models.end())
        return nullptr;
    return model_it->second;
}

bool Renderer::get_render_flag(Name flag) const
{
    checkf(main_render_graph != nullptr, "Graph is null");
    
    return main_render_graph->get_render_flag(flag);
}
