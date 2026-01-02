export module rhcomponents:rhcomp_light;


import rhmath;
import assets;
import framework;
import render_scene;

#include "object/object_reflection_macro.h"

export struct SceneViewProxy_Light : public SceneViewProxy_Transform
{
    vec4 color;
    float intensity;
    float falloff;
};


export class RhComp_Light : public RhComp_Renderable
{
public:    
    RhComp_Light();
    void start() override;
    void finish() override;

    void update_scene_proxy();
    
    vec4 color;
    float intensity;
    float falloff;
    
    SceneViewProxy_Light scene_proxy;
};

REFLECT_OBJECT_FIELDS(RhComp_Light, RhComp_Renderable, 
    transform, color, intensity, falloff);
