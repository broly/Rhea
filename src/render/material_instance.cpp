module render:material_instance;
import assets;
#include "common/assertion_macros.h"
import :material_manager;
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
    material_manager = renderer->get_material_manager();
    
    
    material_id = material_manager->allocate_material();

}

void MaterialInstance::bind(RBCommandList cmd, RBFrameHandle frame)
{
    std::shared_ptr<RenderResourceInstance> instance = get_or_create_resource_instance(frame);

    instance->bind(cmd, frame);
}

void MaterialInstance::apply_material_parameters(
    std::shared_ptr<RenderResourceInstance> instance,
    RBFrameHandle frame)
{
    auto textures_array_resource = renderer->find_resource("textures");
    
    std::optional<Name> material_resource_name = model->material_resource;
    checkf(material_resource_name.has_value(), "material_resource not set");
    
    auto resource_info = renderer->find_resource_info(*material_resource_name);
    
    
    checkf(model->material_info.has_value(), "material_info not set");

    const reflect::RuntimeReflectionInfo& material_structure = 
        reflect::find_runtime_info_checked(model->material_info->structure);
    GPUMaterial gpu_material;
    for (auto& [info_param_name, info] : model->material_info->params)
    {
        const reflect::FieldRuntimeReflectionInfo& field_info = material_structure.find_field_checked(info.member);
        void* value_ptr = field_info.get_value_ptr(&gpu_material);
        const size_t param_size = get_mat_param_size(info.type);
        void* value_ptr_with_offset = ((uint8_t*)value_ptr) + param_size * info.offset;
        
        for (const auto& [param_name, param_value] : material->parameters)
        {
            if (param_name == info_param_name)
            {
                if (param_value.is<TextureHandle>())
                {
                    auto texture_handle = param_value.as<TextureHandle>();
                    if (texture_handle.is_valid() && !texture_handle.is_pending())
                    {
                        uint32_t texture_id = texture_handle.id;
                        memcpy(value_ptr_with_offset, &texture_id, param_size);

                        RBImageHandle image = renderer->get_texture(texture_handle);
                        textures_array_resource->update_image(
                            "u_textures_array",
                            image,
                            {.array_index=texture_id}
                        );
                    } else
                    {
                        uint32_t texture_id = 0;
                        memcpy(value_ptr_with_offset, &texture_id, param_size);
                    }
                }
                else
                    memcpy(value_ptr_with_offset, param_value.get_raw(), param_size);
            }
        }
    }
    material_manager->update_material(material_id, gpu_material);
    
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
            for (const reflect::FieldRuntimeReflectionInfo& field : uniform_buffer_info->fields)
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
        else if (param.type == MaterialParamType::sampler || param.type == MaterialParamType::image)
        {
            auto texture_handle = std::get<TextureHandle>(param_value.data);
            instance->update_image(
                *param.variable,
                renderer->get_texture(texture_handle),
                {
                    .frame = frame
                }
            );
        }
    }
}

std::shared_ptr<RenderResourceInstance> MaterialInstance::get_or_create_resource_instance(RBFrameHandle frame)
{
    if (render_resource_instance != nullptr)
        return render_resource_instance;
    
    render_resource_instance = render_resource->query_unique(material->unique_id, 0);

    // auto instance = pipeline->create_resource_instance(render_resource);
    apply_material_parameters(render_resource_instance, frame);

    // instances.emplace(pipeline, std::move(instance));
    return render_resource_instance;
}
