#include "world.h"

void World::tick()
{
    for (auto& script : scripts)
        script->tick(clock->get_delta_seconds());
}

void World::init()
{    
    Transform transform{ {0.f, 0.f, 0.f}};
    camera = std::make_shared<Camera>(transform);
    
    for (auto& script : scripts)
    {
        script->init(this->shared_from_this());
    }
}
