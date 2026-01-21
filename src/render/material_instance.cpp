module render:material_instance;
import assets;
#include "common/assertion_macros.h"

MaterialInstance::MaterialInstance(const std::shared_ptr<const Material>& in_material, std::shared_ptr<Renderer> in_renderer, Name pass_name)
    : material(in_material)
    , renderer(in_renderer)
{
    Name model_name = material->model;
    auto model_it = renderer->models.find(model_name);
    checkf(model_it != renderer->models.end(), "Could not find specified model");
    
    model = renderer->models.at(material->model);

    render_resource = renderer->get_or_create_resource_from_model(model, pass_name);

}

void MaterialInstance::bind(PipelineObject* pipeline, RBCommandList cmd, RBFrameHandle frame)
{
    RenderResourceInstance* instance =
        get_or_create_resource_instance(pipeline, frame);

    instance->bind(pipeline, cmd, frame);
}

void MaterialInstance::apply_material_parameters(
    RenderResourceInstance* instance,
    PipelineObject* pipeline,
    RBFrameHandle frame)
{

    for (const auto& [param_name, param_value] : material->parameters)
    {
        auto it = model->parameters.find(param_name);
        if (it == model->parameters.end())
            continue;

        const MatModel_Parameter& param = it->second;

        // -------- UBO --------
        if (param.ubo)  // todo: hack
        {
            auto uniform_buffer_it = model->uniform_buffers.find(*param.ubo);
            volatile auto uniform_buffer_info = reflect::find_runtime_info(uniform_buffer_it->second.type_name);
            
            if (!cached_ubos.contains(*param.ubo))
            {
                std::vector<std::byte> data;
                data.resize(uniform_buffer_info->size, std::byte{});
                cached_ubos.insert({*param.ubo, data});
            }
            auto& buf = cached_ubos.at(*param.ubo);
            float* qwe = reinterpret_cast<float*>(buf.data());
            
            for (auto& field : uniform_buffer_info->fields)
            {
                if (field.name == param_name)
                {
                    memcpy(field.get_value_ptr(buf.data()), param_value.get_raw(), 4);
                }
            }
            
            instance->update_uniform_buffer_impl(
                pipeline,
                *param.ubo,
                uniform_buffer_info->size,
                buf.data(),
                frame
            );
            continue;
        }

        // -------- Texture --------
        if (param.binding)  // todo: hack
        {
            auto tex = std::get<TextureHandle>(param_value.data);
            instance->update_image(
                pipeline,
                *param.shader_parameter,
                renderer->get_texture(tex),
                frame
            );
            continue;
        }
    }
}

RenderResourceInstance* MaterialInstance::get_or_create_resource_instance(PipelineObject* pipeline, RBFrameHandle frame)
{
    auto it = instances.find(pipeline);
    if (it != instances.end())
        return it->second.get();

    render_resource->provide(pipeline);
    auto instance = render_resource->create_instance();
    apply_material_parameters(instance, pipeline, frame);

    instances.emplace(pipeline, std::move(instance));
    return instance;
}
