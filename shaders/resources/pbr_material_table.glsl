#ifndef PBR_MATERIAL_TABLE
#define PBR_MATERIAL_TABLE


#ifndef SET_SSBO_PBR
    #define SET_SSBO_PBR 0
    #error "SET_SSBO_PBR must be provided"
#endif
#ifndef BINDING_SSBO_MATERIAL
    #define BINDING_SSBO_MATERIAL 0
    #error "BINDING_SSBO_MATERIAL must be provided"
#endif
#ifndef SET_TEXTURES
    #define SET_TEXTURES 0
    #error "SET_TEXTURES must be provided"
#endif
#ifndef BINDING_ARRAY_TEXTURES
    #define BINDING_ARRAY_TEXTURES 0
    #error "BINDING_ARRAY_TEXTURES must be provided"
#endif
    
    
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

GPUMaterial get_material(uint index)
{
    return materials.materials[nonuniformEXT(index)];
}


vec4 get_base_color(in GPUMaterial mat, vec2 uv)
{
    uint base_color_tex_index = mat.textures0.x;
    vec4 base_color = read_texture(base_color_tex_index, uv);
    return base_color * mat.params0.x;
}


vec4 get_normal(in GPUMaterial mat, vec2 uv)
{
    uint normal_tex_index = mat.textures0.y;
    vec4 normal = read_texture(normal_tex_index, uv);
    return normal;
}



vec4 get_emissive(in GPUMaterial mat, vec2 uv)
{
    uint emissive_color_tex_index = mat.textures0.w;
    vec4 emissive_color = read_texture(emissive_color_tex_index, uv);
    return emissive_color * mat.params0.y;
}


vec3 get_orm(in GPUMaterial mat, vec2 uv)
{
    uint orm_tex_index = mat.textures0.z;
    vec3 orm_color = read_texture(orm_tex_index, uv).rgb;
    vec3 orm_factor = mat.params1.xyz;
    return orm_color * orm_factor;
}
    
    
#endif // PBR_MATERIAL_TABLE