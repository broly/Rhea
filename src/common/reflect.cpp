module reflect;

import <map>;

auto& get_types_registry()
{
    static std::map<TypeId, reflect::RuntimeReflectionInfo> info = {};
    
    return info;
}



bool reflect::register_type(TypeId id, size_t size, type_initializer initializer, 
        const std::vector<FieldRuntimeReflectionInfo>& fields)
{
    auto& registry = get_types_registry();
    
    registry.insert({id, RuntimeReflectionInfo{
        .id = id,
        .size = size,
        .initializer = initializer,
        .is_basic = false,
        .fields = fields
    }});
    
    return true;
}

bool reflect::register_basic_type(TypeId id, size_t size, type_initializer initializer)
{
    auto& registry = get_types_registry();
    
    registry.insert({id, RuntimeReflectionInfo{
        .id = id,
        .size = size,
        .initializer = initializer,
        .is_basic = true,
        .fields = {}
    }});
    
    return true;
}

reflect::RuntimeReflectionInfo* reflect::find_runtime_info(TypeId type_id)
{
    auto& registry = get_types_registry();
    
    auto it = registry.find(type_id);
    if (it != registry.end())
        return &it->second;
    return nullptr;
}
