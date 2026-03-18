module render:material_instance;
import assets;
#include "common/assertion_macros.h"
import :material_manager;
import log;
#include "logging/log_macro.h"

DEFINE_LOGGER(LogMaterialInstance, Warning);

MaterialInstance::MaterialInstance(const std::shared_ptr<const Material>& in_material, 
    std::shared_ptr<Renderer> in_renderer, Name pass_name)
    : material(in_material)
    , renderer(in_renderer)
{
    Name model_name = material->model;
    model = renderer->find_model(model_name);
    checkf(model, "Could not find specified model");

    material_manager = renderer->get_material_manager();
    
    
    material_id = material_manager->allocate_material();

}

void MaterialInstance::apply_material_parameters()
{
    auto textures_array_resource = renderer->find_resource("textures");
    
    
    
    checkf(model->material_info.has_value(), "material_info not set");

    const reflect::RuntimeReflectionInfo& material_structure = 
        reflect::find_runtime_info_checked(model->material_info->structure);
    GPUMaterial gpu_material;
    for (auto& [info_param_name, info] : model->material_info->params)
    {
        if (info.is_static_parameter())
            continue;
        
        const reflect::FieldRuntimeReflectionInfo& field_info = material_structure.find_field_checked(info.get_member_name());
        void* value_ptr = field_info.get_value_ptr(&gpu_material);
        const size_t param_size = get_mat_param_size(info.type);
        void* value_ptr_with_offset = ((uint8_t*)value_ptr) + param_size * info.get_offset();
        
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
    
}