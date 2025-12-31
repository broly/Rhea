module framework:rhcomp_light;



import :actor;
import globals;



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
