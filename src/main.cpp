
import engine;
import <json/value.h>;
import globals;
import <iostream>;
import paths;
import json_utils;
import game;
import name;
import log;
import rhobject;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogMain, DisplayFn);

extern "C" const char* NameDebugResolve(uint32_t id)
{
    return NameDebug::resolve(id);
}

int main() 
{
    LogMain.Log("Engine init");
    paths::init();
    
    auto object_types = reflect::get_subtypes<RhObject>();
    
    LogMain.Log("Known RhObjects (%i):", object_types.size());
    for (auto type : object_types)
        LogMain.Log(" * %s", std::string(type->name).c_str());
    
    const char* engine_config_path = "system/engine.json";
    
    auto value = json_utils::load_json_asset(engine_config_path);
    if (value.has_value())
    {
        if (Json::Value const* engine_class_value = value->find("engine_class"))
        {
            std::string engine_class_name = engine_class_value->asString();
            auto reflection_info = reflect::find_object_reflection_info(engine_class_name);
            std::shared_ptr<Engine> engine = reflection_info->instantiate<Engine>();
            RhGlobals::engine = engine.get();
            engine->run();
        }
        else
        {
            std::cerr << "Could not find json section 'engine_class'" << std::endl;
        }
    } else
    {
        std::cerr << "Could not find asset " << engine_config_path << std::endl;
    }
    return 0;
}
