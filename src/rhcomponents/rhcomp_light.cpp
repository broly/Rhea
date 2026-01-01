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
    
    
    // render_id = RhGlobals::engine->scene_view->register_ro_light();
}

void RhComp_Light::finish()
{
    RhComp_Renderable::finish();
    // RhGlobals::engine->scene_view->unregister_ro_light(render_id);
    
    
}
