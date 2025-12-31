module WorldScript_RotateAroundObject;

import <iostream>;
#include <math.h>
import glm;
import rhmath;

void WorldScript_RotateAroundObject::tick(double dt)
{
    double seconds = world->get_time_seconds();
    const double distance = 10.f;
    
    const double x = cos(seconds) * distance;
    const double z = sin(seconds) * distance;
    
    glm::quat q = math::lookAtQuaternion({-x, 0, z}, {0, 0, 0});
    
    world->camera->transform.set_position({x, 0, z});
    world->camera->transform.set_rotation(q);
}
