#version 450

#if INSTANCE_DISSOLVE
#include "instance_dissolve.glsl"

layout(location = 0) in vec3 v_object_pos;
layout(location = 1) flat in float v_dissolve;
#endif

void main()
{
#if INSTANCE_DISSOLVE
    if (instance_dissolved(instance_dissolve_noise(v_object_pos), v_dissolve))
        discard;
#endif
}
