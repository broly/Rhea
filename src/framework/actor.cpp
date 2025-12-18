#include "actor.h"

#include "common/assertion_macros.h"
#include "components/rhcomp_transform.h"

void RhActor::internal_start()
{
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

void RhActor::import_from_json_object(const Json::Value& object)
{
    if (auto components_json_array_ptr = object.find("components"))
    {
        for (const auto& component_info : *components_json_array_ptr)
        {
            if (auto component_class_value = component_info.find("class");
                ensure(component_class_value && component_class_value->isString()))
            {
                const std::string component_class_name = component_class_value->asString();
                auto reflection_info = reflect::find_object_reflection_info(component_class_name);
                if ensure(reflection_info != nullptr)
                {
                    std::shared_ptr<RhObject> comp_obj = std::invoke(reflection_info->factory);
                    auto component = std::static_pointer_cast<RhComponent>(comp_obj);
                    auto pthis = std::static_pointer_cast<RhActor>(shared_from_this());

                    if (auto fields_object = component_info.find("fields"))
                    {
                        if (reflection_info->serializer != std::nullopt)
                        {
                            std::invoke(reflection_info->serializer.value(), *fields_object, component.get(), true);
                        }
                    }
                    instanced_components.push_back(component);
                    component->on_add(pthis);
                }
            }
        }
    }
}

void RhActor::set_transform(const Transform& transform)
{
    auto transform_comp = find_component<RhComp_Transform>();
    
    assert(transform_comp != nullptr);
    
    
    transform_comp->transform = transform;
}

Transform RhActor::get_transform()
{
    auto transform_comp = find_component<RhComp_Transform>();
    
    assert(transform_comp != nullptr);
    
    return transform_comp->transform;
}
