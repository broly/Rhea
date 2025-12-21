#include "actor.h"

#include "common/assertion_macros.h"
#include "components/rhcomp_transform.h"


void RhActor::internal_start(const std::shared_ptr<World>& in_world)
{
    world = in_world;
    for (auto& instanced_component: instanced_components)
        instanced_component->start();
    start();
}

void RhActor::internal_finish()
{
    for (auto& instanced_component: instanced_components)
        instanced_component->finish();
    finish();
}

void RhActor::internal_tick(double dt)
{
    for (auto& instanced_component: instanced_components)
        instanced_component->tick(dt);
    tick(dt);
}

void RhActor::import_from_json_object(const Json::Value& object, const Json::Value* overrides)
{
    auto pthis = std::static_pointer_cast<RhActor>(shared_from_this());
    
    if (auto components_json_array_ptr = object.find("components"))
    {
        std::set<std::string> processed_component_names;
        for (const auto& component_info : *components_json_array_ptr)
        {
            if (auto component_class_value = component_info.find("class");
                ensure(component_class_value && component_class_value->isString()))
            {
                const std::string component_name = component_info["name"].as<std::string>();
                const std::string component_class_name = component_class_value->asString();
                
                auto reflection_info = reflect::find_object_reflection_info(component_class_name);
                if ensure(reflection_info != nullptr)
                {
                    ObjectInitData InitData {
                        component_name,
                    };
                    
                    std::shared_ptr<RhObject> comp_obj = std::invoke(reflection_info->factory, InitData);
                    auto component = std::static_pointer_cast<RhComponent>(comp_obj);

                    if (auto fields_object = component_info.find("fields"))
                    {
                        if (reflection_info->serializer != std::nullopt)
                        {
                            std::invoke(reflection_info->serializer.value(), *fields_object, component.get(), true);
                        }
                    }
                    instanced_components.push_back(component);
                    processed_component_names.emplace(component_name);
                }
            }
        }
    }
    
    if (auto components_json_array_ptr = overrides->find("components"))
    {
        std::set<std::string> processed_component_names;
        for (const auto& component_info : *components_json_array_ptr)
        {
            if (auto component_class_value = component_info.find("class");
                ensure(component_class_value && component_class_value->isString()))
            {
                auto component_name_val = component_info.find("name");
                if (!component_name_val)
                    continue;
                
                std::string component_name = component_name_val->as<std::string>();
                const std::string component_class_name = component_class_value->asString();
                
                auto reflection_info = reflect::find_object_reflection_info(component_class_name);
                if ensure(reflection_info != nullptr)
                {
                    std::shared_ptr<RhObject> comp_obj = find_component_by_name(component_name);
                    
                    bool just_created = false;
                    if (comp_obj == nullptr)
                    {
                        ObjectInitData InitData {
                            component_name,
                        };
                        comp_obj = std::invoke(reflection_info->factory, InitData);
                        just_created = true;
                    }

                    if (auto fields_object = component_info.find("fields"))
                    {
                        if (reflection_info->serializer != std::nullopt)
                        {
                            std::invoke(reflection_info->serializer.value(), *fields_object, comp_obj.get(), true);
                        }
                    }
                    
                    if (just_created)
                    {
                        auto component = std::static_pointer_cast<RhComponent>(comp_obj);
                        instanced_components.push_back(component);
                        processed_component_names.emplace(component_name);
                    }
                }
            }
        }
    }
    
    for (auto& component: instanced_components)
        component->on_add(pthis);
}

void RhActor::set_transform(const Transform& transform)
{
    auto transform_comp = find_component<RhComp_Transform>();
    
    assert(transform_comp != nullptr);
    
    
    transform_comp->set_transform(transform);
}

Transform RhActor::get_transform()
{
    auto transform_comp = find_component<RhComp_Transform>();
    
    assert(transform_comp != nullptr);
    
    return transform_comp->get_transform();
}
