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
    
        
    ObjectInitData init_data {};
    std::shared_ptr<RhObject> obj = std::invoke(reflection_info->factory, init_data);
    
    context.current_file = path.string();
            
    if (root)
    {
        ensure(root.isObject());                
        {
            std::invoke(reflection_info->serializer.value(), root, obj.get(), context);
        }
    }
    return obj;
}
