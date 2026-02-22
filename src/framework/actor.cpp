module framework:actor;

import :rhcomponent;
import :rhcomp_transform;
import :rhcomp_renderable;
import :core;

import <unordered_map>;

import <string>;

import <cassert>;

#include "common/assertion_macros.h"


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

void RhActor::import_from_json_object(const Json::Value& object, const Json::Value* overrides, const SerializationContext& context)
{
    auto pthis = std::static_pointer_cast<RhActor>(shared_from_this());

    std::unordered_map<std::string, std::shared_ptr<RhComponent>> components_by_name;

    auto process_components = [&](const Json::Value& source)
    {
        auto components_json_array_ptr = source.find("components");
        if (!components_json_array_ptr)
            return;

        for (const auto& component_info : *components_json_array_ptr)
        {
            auto name_val = component_info.find("name");
            if (!name_val || !name_val->isString())
                continue;

            const std::string component_name = name_val->asString();

            auto class_val = component_info.find("class");
            if (!class_val || !class_val->isString())
                continue;

            const std::string component_class_name = class_val->asString();

            auto reflection_info = reflect::find_object_reflection_info(component_class_name);
            if (!ensure(reflection_info != nullptr))
                continue;

            std::shared_ptr<RhComponent> component;

            auto it = components_by_name.find(component_name);
            if (it != components_by_name.end())
            {
                component = it->second;
            }
            else
            {
                ObjectInitData init_data { component_name };
                auto obj = std::invoke(reflection_info->factory, init_data);
                component = std::static_pointer_cast<RhComponent>(obj);

                components_by_name.emplace(component_name, component);
                pending_components.push_back(component);
            }

            if (auto fields_object = component_info.find("fields"))
            {
                if (reflection_info->serializer)
                {
                    component->set_owner(pthis);
                    std::invoke(
                        reflection_info->serializer.value(),
                        *fields_object,
                        component.get(),
                        context
                    );
                }
            }
        }
    };

    process_components(object);

    if (overrides)
        process_components(*overrides);
    
}

void RhActor::finish_importing()
{
    auto pthis = std::static_pointer_cast<RhActor>(shared_from_this());
    instanced_components = pending_components;
    pending_components.clear();
    for (auto& component: instanced_components)
        component->on_add(pthis);
}

void RhActor::set_transform(const Transform& transform)
{
    auto transform_comp = find_component<RhComp_Transform>();
    
    assert(transform_comp != nullptr);
    
    
    transform_comp->set_transform(transform);
}

Transform RhActor::get_transform() const
{
    auto transform_comp = find_component<RhComp_Transform>();
    
    assert(transform_comp != nullptr);
    
    return transform_comp->get_transform();
}

AABB RhActor::get_aabb() const
{
    auto t = find_component<RhComp_Renderable>();
    return t->get_aabb();
}

std::shared_ptr<RhComponent> RhActor::find_component_by_name(const std::string& name)
{
    for (auto component : instanced_components)
        if (component->name == name)
            return component;
    return nullptr;
}
