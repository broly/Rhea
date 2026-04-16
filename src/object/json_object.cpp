module rhobject:json_object;

import <iostream>;
import <fstream>;
import <json/value.h>;
import <json/reader.h>;
import <string>;


#include "common/assertion_macros.h"

std::shared_ptr<RhObject> json_object::load_object_impl(std::filesystem::path path,
                                                   const reflect::ObjectReflectionInfo* reflection_info, SerializationContext& context)
{
        
    std::ifstream file(path.string());
    if (!file.is_open()) {
        std::cerr << "Failed to open json object: " << path.string() << std::endl;
        throw std::runtime_error("Failed to open json object");
    }
    
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;
    
    if (!Json::parseFromStream(reader, file, &root, &errs)) {
        std::cerr << "Failed to parse JSON: " << errs << std::endl;
        return nullptr;
    }
    
    auto cls_it = root.find("__class__");
    if (cls_it)
    {
        checkf(cls_it->isString(), "__class__ isn't string");
        auto name_str = cls_it->asString();
        reflection_info = reflect::find_object_reflection_info(name_str);
        checkf(reflection_info != nullptr, "Could not find class with name '%s'", name_str.c_str());
    }
    
        
    ObjectInitData init_data {};
    std::shared_ptr<RhObject> obj = std::invoke(reflection_info->factory, init_data);
    
    context.current_file = path.string();
            
    if (root)
    {
        ensure(root.isObject());                
        {
            if (reflection_info->serializer.has_value())
            {
                std::invoke(reflection_info->serializer.value(), root, obj.get(), context);
            }
        }
    }
    return obj;
}
