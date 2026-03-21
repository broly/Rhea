#ifndef RESOURCES_CAMERA
#define RESOURCES_CAMERA



#ifndef SET_CAMERA
    #define SET_CAMERA 0
    #error "SET_CAMERA definition is missing. Provide this resource: camera"
#endif
#ifndef BINDING_UBO_CAMERA
    #define BINDING_UBO_CAMERA 0
    #error "BINDING_UBO_CAMERA definition is missing. Provide this resource: camera"
#endif

layout(set = SET_CAMERA, binding = BINDING_UBO_CAMERA) 
uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    
    mat4 prev_proj;
    mat4 prev_view;
    
    mat4 inv_view;
    mat4 inv_proj;
    mat4 inv_viewproj;
    
    vec4 camera_pos;
    
    float near;
    float far;
    
} camera_ubo;

#endif // RESOURCES_CAMERA