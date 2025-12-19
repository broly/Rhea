#include "rhcomp_renderable.h"

#include "framework/actor.h"

void RhComp_Renderable::start()
{
    RhComp_Transform::start();
    
    render_state_dirty = true;
}

void RhComp_Renderable::finish()
{
    RhComp_Transform::finish();
    
}

void RhComp_Renderable::set_transform(const Transform& in_transform)
{
    transform = in_transform;
    mark_render_state_dirty();
}

