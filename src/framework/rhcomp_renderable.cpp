module framework:rhcomp_renderable;

import :rhcomp_renderable;
import globals;


RhComp_Renderable::RhComp_Renderable()
{
    renderable = true;
}

void RhComp_Renderable::start()
{
    RhComp_Transform::start();
    
}

void RhComp_Renderable::finish()
{
    RhComp_Transform::finish();
    
}

