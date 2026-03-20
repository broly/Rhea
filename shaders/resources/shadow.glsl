#ifndef RESOURCES_SHADOW
#define RESOURCES_SHADOW

#ifndef SET_SHADOW_RESOURCE
    #define SET_SHADOW_RESOURCE 0
    #error "SET_SHADOW_RESOURCE must be provided"
#endif
#ifndef BINDING_UBO_SHADOW
    #define BINDING_UBO_SHADOW 0
    #error "BINDING_UBO_SHADOW must be provided"
#endif

layout(set = SET_SHADOW_RESOURCE, binding = BINDING_UBO_SHADOW)
uniform sampler2DShadow u_shadow_depth;





float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

mat2 rot2(float a)
{
    float s = sin(a), c = cos(a);
    return mat2(c, -s, s, c);
}

float shadow_factor(vec3 world_pos, vec3 Ng)
{
    vec3 shadow_pos = world_pos + Ng * 0.003;

    vec4 light_clip = light_ubo.dir_light.light_vp * vec4(shadow_pos, 1.0);
    vec3 proj = light_clip.xyz / light_clip.w;

    if (proj.x < -1.0 || proj.x > 1.0 ||
    proj.y < -1.0 || proj.y > 1.0 ||
    proj.z <  0.0 || proj.z > 1.0)
    return 1.0;

    vec2 uv = proj.xy * 0.5 + 0.5;

    float ndotl = max(dot(Ng, -light_ubo.dir_light.direction.xyz), 0.0);
    float bias = mix(0.0005, 0.0025, 1.0 - ndotl);

    float shadow = texture(
        u_shadow_depth,
        vec3(uv, proj.z - bias)
    );

    return shadow;
}


#endif // RESOURCES_SHADOW