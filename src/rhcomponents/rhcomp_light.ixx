export module rhcomponents:rhcomp_light;


import rhmath;
import assets;
import framework;
import render_scene;
import <map>;
import name;

#include "object/object_reflection_macro.h"

export enum class LightType
{
    point,
    directional
};
REFLECT_ENUM(LightType, 
    point, directional);

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
    
    vec4 color;
    float intensity;
    float falloff;
    LightType type = LightType::point;
    
    SceneViewProxy_Light scene_proxy;
};

REFLECT_OBJECT_FIELDS(RhComp_Light, RhComp_Renderable, 
    transform, color, intensity, falloff, type);
