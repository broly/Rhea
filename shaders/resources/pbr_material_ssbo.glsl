#ifndef PBR_MATERIAL_SSBO
#define PBR_MATERIAL_SSBO
    
    
// ================= MATERIAL STRUCT =================

struct GPUMaterial
{
    vec4 params0;
    // x base_color
    // y emissive
    // z (pad)
    // w (pad)

    vec4 params1;
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
    GPUMaterial materials[];
} materials;

// ================= GLOBAL TEXTURE ARRAY =================

layout(set = SET_TEXTURES, binding = BINDING_ARRAY_TEXTURES)
uniform sampler2D u_textures_array[];


vec4 read_texture(uint index, vec2 uv)
{
    return texture(u_textures_array[nonuniformEXT(index)], uv);
}


vec4 get_base_color(uint material_index, vec2 uv)
{
    GPUMaterial mat = materials.materials[nonuniformEXT(material_index)]; 
    uint base_color_tex_index = mat.textures0.x;
    vec4 base_color = read_texture(base_color_tex_index, uv);
    return base_color * mat.params0.x;
}


vec4 get_emissive(uint material_index, vec2 uv)
{
    GPUMaterial mat = materials.materials[nonuniformEXT(material_index)];
    uint emissive_color_tex_index = mat.textures0.w;
    vec4 emissive_color = read_texture(emissive_color_tex_index, uv);
    return emissive_color * mat.params0.y;
}


vec3 get_orm(uint material_index, vec2 uv)
{
    GPUMaterial mat = materials.materials[nonuniformEXT(material_index)];
    uint orm_tex_index = mat.textures0.z;
    vec3 orm_color = read_texture(orm_tex_index, uv).rgb;
    vec3 orm_factor = mat.params1.xyz;
    return orm_color * orm_factor;
}
    
    
#endif // PBR_MATERIAL_SSBO