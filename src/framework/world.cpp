#include "world.h"

#include <fstream>
#include <json/json.h>

#include "actor.h"
#include "object.h"
#include "object_reflection.h"
#include "common/assertion_macros.h"
#include "common/json_utils.h"
#include "common/paths.h"
#include "render/scene_extractor.h"

class RhActor;

void World::tick()
{
    double dt = clock->get_delta_seconds();
    
    for (auto& script : scripts)
        script->tick(dt);
    
    for (auto& actor : actors)
        actor->internal_tick(dt);
    
    render_extractor->perform_extraction();
}

void World::init(const std::shared_ptr<Renderer>& in_renderer)
{    
    render_extractor = std::make_shared<SceneExtractor>(shared_from_this(), in_renderer);
    
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
    return load_level("levels/bootstrap_level.json");
}

bool World::load_level(std::string level_path)
{
    std::optional<Json::Value> root_opt = json_utils::load_json_asset(level_path);
    
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
                        std::invoke(reflection_info->serializer.value(), *ref_fields_object, obj.get(), true);
                    }
                }
            }
            ensure(level_actor_json_value.isObject());
               
            if ( auto level_actor_fields_object = level_actor_json_value.find("fields"))
            {
                if (reflection_info->serializer != std::nullopt)
                {
                    std::invoke(reflection_info->serializer.value(), *level_actor_fields_object, obj.get(), true);
                }
            }
            
            if (obj->is_actor())
            {
                auto actor = std::static_pointer_cast<RhActor>(obj);
                
                ref_json_value_opt.has_value() ?
                    actor->import_from_json_object(*ref_json_value_opt, &level_actor_json_value) :
                    actor->import_from_json_object(level_actor_json_value);
                actors.push_back(actor);
                actor->internal_start(shared_from_this());
            }
        }
        
        
    }
    
    return true;
}

void World::add_actor(std::shared_ptr<RhActor> actor)
{
    actors.push_back(actor);
}
