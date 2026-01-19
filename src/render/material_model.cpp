module render:material_model;
#include "common/assertion_macros.h"


void MaterialModel::on_serialize(DependencyCollector* dc)
{
    RhObject::on_serialize(dc);
    
    for (const auto& [_, ubo_type] : uniform_buffers)
    {
        const reflect::RuntimeReflectionInfo* info = reflect::find_runtime_info(ubo_type.type_name);
        checkf(info != nullptr, "Can't find structure '%s'", ubo_type.type_name.to_string().c_str());
    }
}
