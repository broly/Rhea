module framework:rhcomponents;


import :actor;
import globals;

void RhComp_Transform::set_transform(const Transform& in_transform)
{
    transform = in_transform;
}

Transform RhComp_Transform::get_transform()
{
    return transform;
}


void RhComp_Renderable::start()
{
    RhComp_Transform::start();
    
    render_state_dirty = true;
    render_id = RhGlobals::engine->scene_extractor->register_ro_mesh();
}

void RhComp_Renderable::finish()
{
    RhComp_Transform::finish();
    RhGlobals::engine->scene_extractor->unregister_ro_mesh(render_id);
    
}

void RhComp_Renderable::set_transform(const Transform& in_transform)
{
    transform = in_transform;
    mark_render_state_dirty();
}