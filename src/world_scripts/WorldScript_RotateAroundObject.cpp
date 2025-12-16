#include "WorldScript_RotateAroundObject.h"

#include "framework/world.h"

void WorldScript_RotateAroundObject::tick(double dt)
{
    auto seconds = world->get_time_seconds();
    world->camera->transform.set_position({0.f, 0.f, (world->get_time_seconds() / 2.f) - 2.f});
}
