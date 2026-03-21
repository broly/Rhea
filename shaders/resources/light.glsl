#ifndef RESOURCES_LIGHT
#define RESOURCES_LIGHT

#ifndef SET_LIGHT
    #define SET_LIGHT 0 
    #error "SET_LIGHT definition is missing. Provide this resource: light"
#endif
#ifndef BINDING_UBO_LIGHT
    #define BINDING_UBO_LIGHT 0 
    #error "BINDING_UBO_LIGHT definition is missing. Provide this resource: light"
#endif

struct PointLight
{
    vec4 position;
    vec4 color;
};

struct DirectionalLight
{
    mat4 light_vp;
    vec4 direction;
    vec4 color;
};

layout(set = SET_LIGHT, binding = BINDING_UBO_LIGHT) 
uniform LightUBO
{
    PointLight lights[8];
    int light_count;

    DirectionalLight dir_light;
    int has_dir_light;

} light_ubo;


#endif // RESOURCES_LIGHT