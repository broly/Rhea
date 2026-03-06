#version 460
#extension GL_EXT_ray_tracing : require

hitAttributeEXT vec2 attribs;

layout(location = 0) rayPayloadInEXT vec3 payload;

void main()
{
    vec3 normal = vec3(0.0, 1.0, 0.0);

    float lighting = max(dot(normal, normalize(vec3(1.0,1.0,1.0))), 0.0);

    payload = vec3(lighting);
}