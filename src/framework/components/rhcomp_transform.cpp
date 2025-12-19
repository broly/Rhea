#include "rhcomp_transform.h"

void RhComp_Transform::set_transform(const Transform& in_transform)
{
    transform = in_transform;
}

Transform RhComp_Transform::get_transform()
{
    return transform;
}
