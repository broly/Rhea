module framework:world;

import json_utils;
import rhmath;
import :engine_clock;
import :world_script;
import :actor;
import dependency_collector;
import glm;

import rhobject;
import <json/value.h>;

#include "common/assertion_macros.h"

void World::tick()
{
    double dt = clock->get_delta_seconds();
    
    for (auto& script : scripts)
        script->tick(dt);
    
    for (auto& actor : actors)
        actor->internal_tick(dt);
}

void World::init()
{
    
    load_bootstrap_level();
    
    for (auto& script : scripts)
    {
        script->init(this->shared_from_this());
    }
}

bool World::load_bootstrap_level()
{
    return load_level("levels/bootstrap_level.json");
}

bool World::load_level(std::string level_path)
{
    std::optional<Json::Value> root_opt = json_utils::load_json_asset(level_path);
    
    
    std::vector<std::shared_ptr<RhActor>> pending_actors;
    
    if (!root_opt.has_value())
    {
        return false;
    }

    const Json::Value& root = root_opt.value();

    std::string name = root["name"].asString();
    std::cout << "Name: " << name << std::endl;

    const Json::Value& json_actors = root["actors"];
    
    if (!json_actors.isArray()) 
    {
        std::cerr << "'actors' is not an array" << std::endl;
        return false;
    }

    std::cout << "Number of actors: " << json_actors.size() << std::endl;

    for (const Json::Value& level_actor_json_value : json_actors)
    {
        std::optional<Json::Value> ref_json_value_opt;
        if (auto ref = level_actor_json_value.find("ref"))
        {
            auto str = ref->asString();
            auto opt = json_utils::load_json_asset(str);
            if (opt.has_value())
            {
                ref_json_value_opt = opt.value();
            }
        }

        std::string actor_name = level_actor_json_value["name"].asString();
        
        std::string actor_class = ref_json_value_opt.has_value() ? 
            (*ref_json_value_opt)["class"].asString() :
            level_actor_json_value["class"].asString();
        
        if (actor_class == "")
            actor_class = level_actor_json_value["class"].asString();
        
        std::cout << "Actor: " << actor_class << std::endl;
        auto reflection_info = reflect::find_object_reflection_info(actor_class);
        if ensure(reflection_info != nullptr)
        {
            ObjectInitData init_data {
                actor_name,
            };
            
            std::shared_ptr<RhObject> obj = std::invoke(reflection_info->factory, init_data);
            
            if (ref_json_value_opt.has_value())
            {
                ensure(ref_json_value_opt->isObject());                
                if ( auto ref_fields_object = ref_json_value_opt->find("fields"))
                {
                    if (reflection_info->serializer != std::nullopt)
                    {
                        std::invoke(reflection_info->serializer.value(), *ref_fields_object, obj.get(), true, &collector);
                    }
                }
            }
            ensure(level_actor_json_value.isObject());
               
            if ( auto level_actor_fields_object = level_actor_json_value.find("fields"))
            {
                if (reflection_info->serializer != std::nullopt)
                {
                    std::invoke(reflection_info->serializer.value(), *level_actor_fields_object, obj.get(), true, &collector);
                }
            }
            
            if (obj->is_actor())
            {
                auto actor = std::static_pointer_cast<RhActor>(obj);
                
                ref_json_value_opt.has_value() ?
                    actor->import_from_json_object(*ref_json_value_opt, &level_actor_json_value, &collector) :
                    actor->import_from_json_object(level_actor_json_value, nullptr, &collector);
                pending_actors.push_back(actor);
            }
        }
        
        
    }
    
    collector.wait();
    
    for (uint32_t index = 0; index < pending_actors.size(); index++)
    {
        auto& actor = pending_actors[index];
        actor->finish_importing();
        actors.push_back(actor);
        actor->internal_start(shared_from_this());
    }
    
    return true;
}

void World::add_actor(std::shared_ptr<RhActor> actor)
{
    actors.push_back(actor);
}

std::shared_ptr<RhActor> World::spawn(const std::string& name)
{
    std::shared_ptr<RhActor> actor = std::make_shared<RhActor>();
    actor->name = name;
    actors.push_back(actor);
    actor->internal_start(shared_from_this());
    return actor;
}

AABB World::get_world_aabb() const
{
    AABB result = {glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 0.f)};
    for (auto& actor : actors)
    {
        result += actor->get_aabb();
    }
    return result;
}

double World::get_time_seconds() const
{
    return clock->get_total_seconds();
}

std::shared_ptr<RhActor> World::find_actor_by_name(const std::string& name)
{
    for (auto& actor : actors)
        if (actor->name == name)
            return actor;
    return nullptr;
}
