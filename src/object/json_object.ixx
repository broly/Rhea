export module rhobject:json_object;
import <memory>;
import <filesystem>;

import :reflection;
import json_utils;

namespace json_object
{
    
    std::shared_ptr<RhObject> load_object_impl(std::filesystem::path path, 
        const reflect::ObjectReflectionInfo* reflection_info, SerializationContext& context);

    export template<typename T, typename... Ts>
    std::shared_ptr<T> load_object(std::filesystem::path path, SerializationContext& context, Ts&&... args)
    {
        const reflect::ObjectReflectionInfo* reflection_info = reflect::find_info<T>();

        std::shared_ptr<RhObject> ptr = load_object_impl(path, reflection_info, context);
        
        if (ptr == nullptr)
            return nullptr;
        
        std::shared_ptr<T> result = std::static_pointer_cast<T>(ptr);
        
        if constexpr (requires { result->ctor(std::forward<Ts>(args)...); })
        {
            result->ctor(std::forward<Ts>(args)...);
        }
        return result;
    
    }
}
