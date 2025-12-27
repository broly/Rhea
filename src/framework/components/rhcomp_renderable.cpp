#include "rhcomp_renderable.h"

#include "framework/actor.h"
#include "render/scene_extractor.h"

void RhComp_Renderable::start()
{
    RhComp_Transform::start();
    
    render_state_dirty = true;
    render_id = owner->world->render_extractor->register_ro_mesh();
}

void RhComp_Renderable::finish()
{
    RhComp_Transform::finish();
    owner->world->render_extractor->unregister_ro_mesh(render_id);
    
}

void RhComp_Renderable::set_transform(const Transform& in_transform)
{
    transform = in_transform;
    mark_render_state_dirty();
}

