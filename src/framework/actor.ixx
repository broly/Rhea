export module framework:actor;

import rhobject;
import <memory>;
import <json/value.h>;
import :world;
import :rhcomponent;
import rhmath;
import :core;

#include "object/object_reflection_macro.h"

export class RhActor : public RhObject
{
public:    
    RhActor()
    {
        instanced_components = {};
    }
    
    bool is_actor() const override
    {
        return true;
    }
    
    void internal_start(const std::shared_ptr<World>& in_world);
    void internal_finish();
    void internal_tick(double dt);
    
    void import_from_json_object(const Json::Value& object, const Json::Value* overrides = nullptr);
    
    virtual void start() {}
    virtual void tick(const double DeltaTime) {}
    virtual void finish() {}
    
    void set_transform(const Transform& transform);
    Transform get_transform();
    
    template<typename T>
    std::shared_ptr<T> find_component()
    {
        auto requested_object_type_id = reflect::get_object_type_id<T>();
        for (auto component : instanced_components)
        {
            const auto& component_reflection_info = 
                reflect::find_object_reflection_info(component->get_type_id());
            if (component_reflection_info->bases.contains(requested_object_type_id))
                return std::static_pointer_cast<T>(component);
        }
        return nullptr;
    }
    
    std::shared_ptr<RhComponent> find_component_by_name(const std::string& name);

    std::shared_ptr<World> get_world() const
    {
        return world;
    }
    
    std::vector<std::shared_ptr<RhComponent>> instanced_components;
    std::shared_ptr<World> world;
};


REFLECT_OBJECT(RhActor, RhObject);