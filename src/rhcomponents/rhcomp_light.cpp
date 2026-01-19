module rhcomponents:rhcomp_light;



import globals;
import render;
#include "common/offsetof.h"


void RhComp_Light::on_init()
{
    if (!is_default)
    {
        render_info.scene_proxy_offset = offsetof(RhComp_Light, scene_proxy);
        render_info.is_explicitly_null = false;
        render_info.type_id = reflect::get_default<SceneViewProcessor_Light>()->index;
    }
}

void RhComp_Light::start()
{
    RhComp_Renderable::start();
    update_scene_proxy();
    RhGlobals::engine->scene_view->register_scene_view_proxy(scene_proxy, render_info.type_id, name);
}

void RhComp_Light::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy(scene_proxy, render_info.type_id);
    
    
}

void RhComp_Light::update_scene_proxy()
{
    scene_proxy.transform = transform;
    scene_proxy.color = color;
    scene_proxy.intensity = intensity;
    scene_proxy.falloff = falloff;
}
