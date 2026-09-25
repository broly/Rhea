module;

#include <json/value.h>

export module rhcomponents:rhcomp_camera;

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

#include "object/object_reflection_macro.h"

export struct SceneViewProxy_Camera : public SceneViewProxy_Transform
{
    float fov = 0.f;
    float near_plane = 0.f;
    float far_plane = 0.f;
    bool active = false;
};


export class RhComp_Camera : public RhComp_Renderable
{
public:
    void on_init() override;
    
    void start() override;
    void finish() override;
    
    [[=rh::serialize, =rh::edit, =rh::degrees, =rh::range<10.f, 120.f>]] float fov;
    [[=rh::serialize, =rh::edit, =rh::speed<0.001f>]] float near_plane;
    [[=rh::serialize, =rh::edit, =rh::speed<1.f>]] float far_plane;
    [[=rh::serialize, =rh::edit]] bool active;
    
    void update_scene_proxy();
    void on_property_changed(std::string_view property) override;
    
    SceneViewProxy_Camera scene_proxy;
};

RH_OBJECT(RhComp_Camera)