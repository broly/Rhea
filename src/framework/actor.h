#pragma once
#include "actor_component.h"
#include "object.h"
#include "math/transform.h"

class RhActor : public RhObject
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
    
    void internal_start();
    void internal_finish();
    void internal_tick(double dt);
    
    void import_from_json_object(const Json::Value& object);
    
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
    
    std::vector<std::shared_ptr<RhComponent>> instanced_components;
};

REG_REFLECT(RhActor, RhObject);