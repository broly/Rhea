module framework:rhcomp_transform;
import rhmath;

void RhComp_Transform::set_transform(const Transform& in_transform)
{
    transform = in_transform;
}

Transform RhComp_Transform::get_transform()
{
    return transform;
}