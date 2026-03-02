module render:material_model;
#include "common/assertion_macros.h"


void MatModel_Parameter::validate()
{
    if (type == MaterialParamType::uniform)
    {
        checkf(binding.has_value(), "uniform parameters must have binding");
        checkf(variable.has_value(), "uniform parameters must have variable");
        checkf(ubo.has_value(), "uniform parameters must have UBO");
        checkf(!definition.has_value(), "definition for uniform should be null");
            
        const Name ubo_name = *ubo;
        const reflect::RuntimeReflectionInfo* info = reflect::find_runtime_info(ubo_name);
        checkf(info != nullptr, "Can't find structure '%s'", ubo_name.to_string().c_str());
            
        size = info->size;
            
    } else if (type == MaterialParamType::sampler)
    {
        checkf(binding.has_value(), "sampler parameters must have binding");
        checkf(variable.has_value(), "sampler parameters must have variable");
        checkf(!definition.has_value(), "definition for sampler should be null");
        //checkf(definition.has_value(), "sampler parameters must have definition");
    } else if (type == MaterialParamType::definition)
    {
        checkf(definition.has_value(), "definition parameters must have definition");
    }
}

const PipelineInfo_Graphics* MaterialModel::get_pipeline_by_pass(Name pass_name) const
{
    for (auto& pass : pipelines)
    {
        if (std::get<PipelineInfo_Graphics>(pass).pass == pass_name)
            return &std::get<PipelineInfo_Graphics>(pass);
    }
    return nullptr;
}

void MaterialModel::on_serialize(const SerializationContext& context)
{
    RhObject::on_serialize(context);
    
}

void RenderResourceInfo::on_serialize(const SerializationContext& context)
{
    RhObject::on_serialize(context);
    
    for (auto& [param_name, param] : resource.parameters)
    {
        param.validate();
    }
}
