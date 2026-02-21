#ifndef PBR_MATERIAL_LIGHT
#define PBR_MATERIAL_LIGHT

// ================== MATERIAL ==================
layout(set = SET_PBR, binding = BINDING_MATERIAL) uniform MaterialUBO
{
    float base_color_factor;
    float emissive_factor;
    float occlusion_factor;
    float roughness_factor;
    float metallic_factor;
} material_ubo;

layout(set = SET_PBR, binding = BINDING_SAMPLER_ALBEDO) uniform sampler2D u_base_color;
layout(set = SET_PBR, binding = BINDING_SAMPLER_EMISSIVE) uniform sampler2D u_emissive;
layout(set = SET_PBR, binding = BINDING_SAMPLER_NORMAL) uniform sampler2D u_normal_map;
layout(set = SET_PBR, binding = BINDING_SAMPLER_ORM) uniform sampler2D u_orm;


#endif // PBR_MATERIAL