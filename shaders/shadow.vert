#version 450

#include "definitions.glsl"

layout(location = 0) in vec3 in_position;

layout(push_constant) uniform PC
{
    mat4 model;
} pc;


// ================== LIGHT ==================
struct PointLight
{
    vec4 position;
    vec4 color;
};

struct DirectionalLight
{
    mat4 light_vp;  // view-projection for shadow
    vec4 direction; // xyz normalized (world)
    vec4 color;     // rgb * intensity
};

layout(set = SET_SHADOW_LIGHT, binding = BINDING_UBO_SHADOW_LIGHT) uniform LightUBO
{
    PointLight lights[8];
    int light_count;
    
    DirectionalLight dir_light;
    int has_dir_light;
} shadow_light_ubo;


void main()
{
    gl_Position = shadow_light_ubo.dir_light.light_vp * pc.model * vec4(in_position, 1.0);
}