#ifndef PBR_MATERIAL_SSBO
#define PBR_MATERIAL_SSBO

#extension GL_EXT_nonuniform_qualifier : enable
    
    
// ================= MATERIAL STRUCT =================

struct Material
{
    vec4 base_color_factor;
    vec4 emissive_factor;

    vec4 params0;
    // x roughness
    // y metallic
    // z occlusion
    // w (pad)

    uvec4 textures0;
    // x base_color
    // y normal
    // z orm
    // w emissive
};

// ================= MATERIAL BUFFER =================

layout(std430, set = SET_SSBO_PBR, binding = BINDING_SSBO_MATERIAL)
readonly buffer MaterialBuffer
{
    Material materials[];
};

// ================= GLOBAL TEXTURE ARRAY =================

layout(set = SET_SSBO_PBR, binding = BINDING_SSBO_TEXTURES)
uniform sampler2D textures[];


vec4 read_texture(uint index, vec2 uv)
{
    return texture(textures[nonuniformEXT(index)], uv);
}


vec4 get_base_color(in Material mat, vec2 uv)
{
    uint base_color_tex_index = mat.textures0.x;
    vec4 base_color = read_texture(base_color_tex_index, uv);
    return base_color * mat.base_color_factor;
}


vec4 get_emissive(in Material mat, vec2 uv)
{
    uint emissive_color_tex_index = mat.textures0.w;
    vec4 emissive_color = read_texture(emissive_color_tex_index, uv);
    return emissive_color * mat.emissive_factor;
}


vec3 get_orm(in Material mat, vec2 uv)
{
    uint orm_tex_index = mat.textures0.z;
    vec3 orm_color = read_texture(orm_tex_index, uv).rgb;
    vec3 orm_factor = mat.params0.xyz;
    return orm_color * orm_factor;
}
    
    
#endif // PBR_MATERIAL_SSBO