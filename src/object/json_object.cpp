module rhobject:json_object;

import <iostream>;
import <fstream>;
import <json/value.h>;
import <json/reader.h>;
import <string>;


#include "common/assertion_macros.h"

std::shared_ptr<RhObject> json_object::load_object_impl(std::filesystem::path path,
                                                   const reflect::ObjectReflectionInfo* reflection_info, DependencyCollector* collector)
{
        
    std::ifstream file(path.string());
    if (!file.is_open()) {
        std::cerr << "Failed to open bootstrap_level.json" << std::endl;
        return nullptr;
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
            
    if (root)
    {
        ensure(root.isObject());                
        {
            std::invoke(reflection_info->serializer.value(), root, obj.get(), true, collector);
        }
    }
    return obj;
}
