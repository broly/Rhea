module rhobject:reflection;

import <map>;

static auto& get_registry() 
{
    static std::map<Name, reflect::ObjectReflectionInfo> registry;
    return registry;
}

void reflect::register_object_class_impl(
    std::string_view name, 
    ObjectFactoryType&& factory, 
    UniqueObjectFactoryType&& unique_factory, 
    std::set<std::string_view>&& bases,
    std::optional<JsonSerializer> serializer)
{
    auto data = 
        ObjectReflectionInfo{
            name,
            std::move(bases),
            std::move(factory),
            std::move(unique_factory),
            std::move(serializer)
        };
    
    Name in_name = name;
    get_registry().insert({in_name, std::move(data)});
}

std::vector<const reflect::ObjectReflectionInfo*> reflect::get_subtypes(Name type_name, bool include_parent)
{
    std::vector<const reflect::ObjectReflectionInfo*> subtypes;
    for (auto& [name, info] : get_registry())
    {
        if (type_name == name && !include_parent)
            continue;
        
        if (info.bases.contains(type_name.to_string()))
        {
            subtypes.push_back(&info);
        }
    }
    return subtypes;
}

void reflect::create_defaults()
{
    for (auto& [name, info] : get_registry())
        info.instantiate_default();
}

void reflect::ObjectReflectionInfo::instantiate_default()
{
    std::string obj_name = std::string(name) + "_default";
    ObjectInitData init_data;
    init_data.name = obj_name;
    init_data.is_default = true;
    default_object = (unique_factory)(init_data);
}

const reflect::ObjectReflectionInfo* reflect::find_object_reflection_info(Name name)
{
    const auto& registry = get_registry();
    if (registry.contains(name))
    {
        return &registry.at(name);
    }
    return nullptr;
}

