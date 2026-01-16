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
    std::set<std::string_view>&& bases,
    std::optional<JsonSerializer> serializer)
{
    auto data = 
        ObjectReflectionInfo{
            name,
            std::move(bases),
            std::move(factory),
            std::move(serializer)
        };
    get_registry().insert({name, data});
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

const reflect::ObjectReflectionInfo* reflect::find_object_reflection_info(Name name)
{
    const auto& registry = get_registry();
    if (registry.contains(name))
    {
        return &registry.at(name);
    }
    return nullptr;
}

