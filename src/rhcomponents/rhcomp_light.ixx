module;

#include <json/value.h>

export module rhcomponents:rhcomp_light;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;


import rhmath;
import assets;
import framework;
import render_scene;
import name;

#include "object/object_reflection_macro.h"

export enum class LightType
{
    point,
    directional
};


export struct SceneViewProxy_Light : public SceneViewProxy_Transform
{
    LightType light_type;
    vec4 color;
    float intensity;
    float falloff;
};


export class RhComp_Light : public RhComp_Renderable
{
public:    
    void on_init() override;
    void start() override;
    void finish() override;

    void update_scene_proxy();
    
    void on_serialize(const SerializationContext& context) override;
    void on_property_changed(std::string_view property) override;
    
    [[=rh::serialize, =rh::edit, =rh::color]] vec4 color;
    [[=rh::serialize, =rh::edit, =rh::speed<0.05f>]] float intensity;
    [[=rh::serialize, =rh::edit, =rh::speed<0.01f>]] float falloff;
    [[=rh::serialize, =rh::edit]] LightType type = LightType::point;
    
    SceneViewProxy_Light scene_proxy;
};

RH_OBJECT(RhComp_Light)
