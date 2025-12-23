#include "WorldScript_RotateAroundObject.h"

#include "framework/actor.h"
#include "framework/world.h"

void WorldScript_RotateAroundObject::tick(double dt)
{
    double seconds = world->get_time_seconds();
    double distance = seconds * 5.f - 2.f;
    world->camera->transform.set_position({0.f, 0.f, distance});
}
