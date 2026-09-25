module;

#include <json/value.h>

export module rail;

import std.compat;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;
import framework;
import glm;
import name;
import rhmath;
import rhobject;
#include "object/object_reflection_macro.h"

export struct RailSampleData
{
    vec3 position;
    quat rotation;
    vec4 color;
};


export struct RailSample : RailSampleData
{
    float timestamp_seconds;
};


export using RailCallback = std::function<void(const RailSampleData& data)>;

export class Rail : public RhActor
{
public:
    [[=rh::serialize]] 
    std::map<Name, std::vector<RailSample>> samples;
    
    [[=rh::edit, =rh::read_only]] 
    float accumulated_time = 0.0f;
    
    [[=rh::serialize, =rh::edit, =rh::range<0.f, 5.f>]] 
    float time_dilation = 1.0f;
    
    [[=rh::serialize, =rh::edit]] 
    bool fixed_timestep = false;
    
    void set_accum_time(float t);
    
    [[=rh::serialize]] 
    std::optional<float> timestep = std::nullopt;
    
    void tick(const double dt) override;
    
    void startup();
    
    void on_serialize(const SerializationContext& context) override;
    
    [[=rh::edit, =rh::read_only]] 
    bool active = false;
    
    float start_time = 0;
    
    float get_passed_time() const;
    
    void add_on_tick(Name name, RailCallback cb)
    {
        on_tick_map.insert({name, cb});
    }
    
    [[=rh::edit]] 
    bool loop = false;
    
    std::map<Name, RailCallback> on_tick_map;
};
RH_OBJECT(Rail)