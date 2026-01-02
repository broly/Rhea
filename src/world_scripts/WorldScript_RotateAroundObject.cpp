module WorldScript_RotateAroundObject;

import <iostream>;
#include <math.h>
import glm;
import rhmath;

void WorldScript_RotateAroundObject::tick(double dt)
{
    auto camera_actor = world->find_actor_by_name("viewer");
    if (camera_actor)
    {
        double seconds = world->get_time_seconds();
        const double distance = 10.f;
    
        const double x = cos(seconds) * distance;
        const double z = sin(seconds) * distance;
        
        glm::vec3 position = {x, 0, z};
        glm::vec3 target   = {0, 0, 0};
        glm::vec3 dir = glm::normalize(target - position);
    
        glm::quat quat = math::lookRotation(-dir);
        glm::vec3 location = {x, 0, z};
        
        Transform t{location, quat};
    
        camera_actor->set_transform(t);
    }
}
