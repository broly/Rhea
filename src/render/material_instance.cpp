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
        if (param.type == MaterialParamType::uniform)
        {
            checkf(param.ubo.has_value(), "Uniform buffer parameter has no value");
            const Name ubo_name = *param.ubo;

            const reflect::RuntimeReflectionInfo* uniform_buffer_info = reflect::find_runtime_info(ubo_name);
            
            if (!cached_ubos.contains(ubo_name))
            {
                std::vector<std::byte> data;
                data.resize(uniform_buffer_info->size, std::byte{});
                cached_ubos.insert({ubo_name, data});
            }
            auto& buf = cached_ubos.at(ubo_name);
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
        }

        // -------- Texture --------
        else if (param.type == MaterialParamType::sampler)
        {
            auto tex = std::get<TextureHandle>(param_value.data);
            instance->update_image(
                pipeline,
                *param.variable,
                renderer->get_texture(tex),
                frame
            );
        }
    }
}

RenderResourceInstance* MaterialInstance::get_or_create_resource_instance(PipelineObject* pipeline, RBFrameHandle frame)
{
    auto it = instances.find(pipeline);
    if (it != instances.end())
        return it->second.get();

    auto instance = pipeline->create_resource_instance(render_resource);
    apply_material_parameters(instance, pipeline, frame);

    instances.emplace(pipeline, std::move(instance));
    return instance;
}
