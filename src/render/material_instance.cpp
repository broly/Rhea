module render:material_instance;
import assets;
#include "common/assertion_macros.h"
import log;
#include "logging/log_macro.h"

DEFINE_LOGGER(LogMaterialInstance, Warning);

MaterialInstance::MaterialInstance(const std::shared_ptr<const Material>& in_material, 
    std::shared_ptr<Renderer> in_renderer, Name pass_name, std::shared_ptr<PipelineFamily> in_pipeline_family)
    : material(in_material)
    , renderer(in_renderer)
    , pipeline_family(in_pipeline_family)
{
    Name model_name = material->model;
    model = renderer->find_model(model_name);
    checkf(model, "Could not find specified model");
    
    render_resource = renderer->query_material_resource(model, pass_name);
    
    static uint32_t unique_id_counter = 0;
    
    unique_id = unique_id_counter++;

}

void MaterialInstance::bind(RBCommandList cmd, RBFrameHandle frame)
{
    if (material->parameters.contains("base_color"))
    {
        LogMaterialInstance.Log("bind material instance. BaseColor: %s", 
            material->parameters.at("base_color").as<TextureHandle>().get().name.c_str());
    }
    std::shared_ptr<RenderResourceInstance> instance = get_or_create_resource_instance(frame);

    instance->bind(cmd, frame);
}

void MaterialInstance::apply_material_parameters(
    std::shared_ptr<RenderResourceInstance> instance,
    RBFrameHandle frame)
{
    std::optional<Name> material_resource_name = model->material_resource;
    checkf(material_resource_name.has_value(), "material_resource not set");
    
    auto resource_info = renderer->find_resource_info(*material_resource_name);
    
    for (const auto& [param_name, param_value] : material->parameters)
    {
        
        auto it = resource_info->resource.parameters.find(param_name);
        if (it == resource_info->resource.parameters.end())
            continue;

        const MatModel_Parameter& param = it->second;

        // -------- UBO --------
        if (param.type == MaterialParamType::uniform)
        {

        } 
        
        else if (param.type == MaterialParamType::vec4)
        {
            checkf(param.storage.has_value(), "vec4 parameter must have storage");
            
            auto storage_it = resource_info->resource.parameters.find(*param.storage);
            
            checkf(storage_it != resource_info->resource.parameters.end(), "unknown storage %s", param.storage->to_string().c_str());
            
            auto ubo = storage_it->second.ubo;
            
            checkf(ubo.has_value(), "ubo not set");
            
            const Name ubo_name = *ubo;
            
            const Name ubo_var_name = *storage_it->second.variable;
            
            const reflect::RuntimeReflectionInfo* uniform_buffer_info = reflect::find_runtime_info(ubo_name);
            if (!cached_ubos.contains(ubo_name))
            {
                std::vector<std::byte> data;
                data.resize(uniform_buffer_info->size, std::byte{});
                cached_ubos.insert({ubo_name, data});
            }
            auto& buf = cached_ubos.at(ubo_name);
            for (auto& field : uniform_buffer_info->fields)
            {
                if (field.name == param_name)
                {
                    memcpy(field.get_value_ptr(buf.data()), param_value.get_raw(), 4);
                }
            }
            instance->update_uniform_buffer_impl(
                ubo_var_name,
                uniform_buffer_info->size,
                buf.data(),
                frame
            );
        }

        // -------- Texture --------
        else if (param.type == MaterialParamType::sampler)
        {
            auto tex = std::get<TextureHandle>(param_value.data);
            instance->update_image(
                *param.variable,
                renderer->get_texture(tex),
                frame
            );
        }
    }
}

std::shared_ptr<RenderResourceInstance> MaterialInstance::get_or_create_resource_instance(RBFrameHandle frame)
{
    if (render_resource_instance != nullptr)
        return render_resource_instance;
    
    render_resource_instance = render_resource->query_unique(pipeline_family->get_pipeline_layout(), material->unique_id, 0);

    // auto instance = pipeline->create_resource_instance(render_resource);
    apply_material_parameters(render_resource_instance, frame);

    // instances.emplace(pipeline, std::move(instance));
    return render_resource_instance;
}
