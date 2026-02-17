#ifndef RESOURCES_CAMERA
#define RESOURCES_CAMERA

layout(set = SET_CAMERA, binding = BINDING_UBO_CAMERA) 
uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    vec4 camera_pos;
} camera_ubo;

#endif // RESOURCES_CAMERA