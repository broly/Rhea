#include "object_reflection.h"

#include <map>

static auto& get_registry() 
{
    static std::map<std::string_view, reflect::ObjectReflectionInfo> registry;
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

const reflect::ObjectReflectionInfo* reflect::find_object_reflection_info(std::string_view name)
{
    const auto& registry = get_registry();
    if (registry.contains(name))
    {
        return &registry.at(name);
    }
    return nullptr;
}

