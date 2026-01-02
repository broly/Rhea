module rhcomponents:rhcomp_light;



import globals;
#include "common/offsetof.h"



RhComp_Light::RhComp_Light()
{
    render_info.scene_proxy_offset = offsetof(RhComp_Light, scene_proxy);
    render_info.is_explicitly_null = false;
    render_info.processor_id = 2;  // SceneViewProcessor_Light
}

void RhComp_Light::start()
{
    RhComp_Renderable::start();
    update_scene_proxy();
    RhGlobals::engine->scene_view->register_scene_view_proxy(scene_proxy, render_info.processor_id, name);
}

void RhComp_Light::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy(scene_proxy, render_info.processor_id);
    
    
}

void RhComp_Light::update_scene_proxy()
{
    scene_proxy.transform = transform;
    scene_proxy.color = color;
    scene_proxy.intensity = intensity;
    scene_proxy.falloff = falloff;
}
