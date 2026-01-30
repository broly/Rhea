export module rhcomponents:rhcomp_reflection_capture;


import rhmath;
import assets;
import framework;
import render_scene;

#include "object/object_reflection_macro.h"

export struct SceneViewProxy_ReflectionCapture : public SceneViewProxy_Transform
{
    float fov = 0.f;
    float near_plane = 0.f;
    float far_plane = 0.f;
    bool active = false;
};



export class RhComp_ReflectionCapture : public RhComp_Renderable
{
public:
    void on_init() override;
    
    void start() override;
    void finish() override;
    
    float fov;
    float near_plane;
    float far_plane;
    bool active;
    
    void update_scene_proxy();
    
    SceneViewProxy_ReflectionCapture scene_proxy;
};

REFLECT_OBJECT_FIELDS(RhComp_ReflectionCapture, RhComp_Renderable, 
    transform, fov, near_plane, far_plane, active);