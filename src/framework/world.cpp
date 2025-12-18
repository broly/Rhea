#include "world.h"

#include <fstream>
#include <json/json.h>

#include "actor.h"
#include "object.h"
#include "object_reflection.h"
#include "common/assertion_macros.h"
#include "common/paths.h"

class RhActor;

void World::tick()
{
    for (auto& script : scripts)
        script->tick(clock->get_delta_seconds());
}

void World::init()
{    
    load_bootstrap_level();
    
    Transform transform{ {0.f, 0.f, 0.f}};
    camera = std::make_shared<Camera>(transform);
    
    for (auto& script : scripts)
    {
        script->init(this->shared_from_this());
    }
}

bool World::load_bootstrap_level()
{
    return load_level(paths::get_project_path() / "bootstrap_level.json");
}

bool World::load_level(std::filesystem::path level_path)
{
    std::ifstream file(level_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open bootstrap_level.json" << std::endl;
        return false;
    }
    
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;
    
    if (!Json::parseFromStream(reader, file, &root, &errs)) {
        std::cerr << "Failed to parse JSON: " << errs << std::endl;
        return false;
    }

    std::string name = root["name"].asString();
    std::cout << "Name: " << name << std::endl;

    const Json::Value& json_actors = root["actors"];
    
    if (!json_actors.isArray()) 
    {
        std::cerr << "'actors' is not an array" << std::endl;
        return false;
    }

    std::cout << "Number of actors: " << json_actors.size() << std::endl;

    for (const Json::Value& actor_json_value : json_actors)
    {
        std::string actor_class = actor_json_value["class"].asString();
        std::cout << "Actor: " << actor_class << std::endl;
        auto reflection_info = reflect::find_object_reflection_info(actor_class);
        if ensure(reflection_info != nullptr)
        {
            std::shared_ptr<RhObject> obj = std::invoke(reflection_info->factory);
            
            ensure(actor_json_value.isObject());
            
            auto fields_object = actor_json_value.find("fields");
            
            
            if (fields_object != nullptr)
            {
                if (reflection_info->serializer != std::nullopt)
                {
                    std::invoke(reflection_info->serializer.value(), *fields_object, obj.get(), true);
                }
            }
            if (obj->is_actor())
            {
                auto actor = std::static_pointer_cast<RhActor>(obj);
                actor->import_from_json_object(actor_json_value);
                actors.push_back(actor);
                actor->start();
            }
        }
        
        
    }
    
    return true;
}

void World::add_actor(std::shared_ptr<RhActor> actor)
{
    actors.push_back(actor);
}
