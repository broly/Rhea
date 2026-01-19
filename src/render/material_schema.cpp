module render:material_schema;



void MaterialSchema::on_serialize(DependencyCollector* dc)
{
    RhObject::on_serialize(dc);
    
    for (auto [ubo_name, ubo_type] : uniform_buffers)
    {
        auto result = reflect::find_runtime_info(ubo_type);
    }
}