#ifndef DEFINITIONS
#define DEFINITIONS


// geometry.vert / geometry.frag

#define SET_CAMERA 0
    #define BINDING_CAMERA_UBO 0

#define SET_MATERIAL 1
    #define BINDING_MATERIAL_UBO 0
    #define BINDING_BASE_COLOR 1
    #define BINDING_EMISSIVE 2
    #define BINDING_NORMAL_MAP 3
    #define BINDING_ORM 4

#define SET_LIGHT 2
    #define BINDING_LIGHT_UBO 0

// tonemap.frag

#define SET_TONEMAPPING 0
    #define BINDING_HDR_COLOR 0

const float PI = 3.14159265359;

#endif