#ifndef RESOURCES_CAMERA
#define RESOURCES_CAMERA

layout(set = SET_CAMERA, binding = BINDING_UBO_CAMERA) 
uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    
    mat4 prev_proj;
    mat4 prev_view;
    
    vec4 camera_pos;
    
    float near;
    float far;
    
} camera_ubo;

#endif // RESOURCES_CAMERA