export module rhobject:json_object;
import <memory>;
import <filesystem>;

import :reflection;
import json_utils;

namespace json_object
{
    
    std::shared_ptr<RhObject> load_object_impl(std::filesystem::path path, 
        const reflect::ObjectReflectionInfo* reflection_info, const SerializationContext& context);

    export template<typename T>
    std::shared_ptr<T> load_object(std::filesystem::path path, const SerializationContext& context)
    {
        const reflect::ObjectReflectionInfo* reflection_info = reflect::find_info<T>();
        
        return std::static_pointer_cast<T>(load_object_impl(path, reflection_info, context));
    
    }
}
