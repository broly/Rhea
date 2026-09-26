#ifndef PBR_MATERIAL_TABLE
#define PBR_MATERIAL_TABLE


#ifndef SET_SSBO_PBR
    #define SET_SSBO_PBR 0
    #error "SET_SSBO_PBR definition is missing. Provide this resource: pbr_material_table"
#endif
#ifndef BINDING_SSBO_MATERIAL
    #define BINDING_SSBO_MATERIAL 0
    #error "BINDING_SSBO_MATERIAL definition is missing. Provide this resource: pbr_material_table"
#endif
#ifndef SET_TEXTURES
    #define SET_TEXTURES 0
    #error "SET_TEXTURES definition is missing. Provide this resource: pbr_material_table"
#endif
#ifndef BINDING_ARRAY_TEXTURES
    #define BINDING_ARRAY_TEXTURES 0
    #error "BINDING_ARRAY_TEXTURES definition is missing. Provide this resource: pbr_material_table"
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

    // ---- extended block (used by non-legacy material models, see character.json) ----
    uvec4 textures1;
    vec4 params2;
    vec4 params3;
    vec4 params4;
    vec4 params5;
    vec4 params6;
    vec4 params7;
    vec4 params8;
    vec4 params9;
    vec4 params10;
    vec4 params11;
    vec4 params12;
    vec4 params13;
    vec4 params14;
    vec4 params15;
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

// Index 0 means "no texture" (texture ids start at 1): the fallback is used, the material factors apply.
// Slot 0 holds a black 1x1 texture (Renderer::write_null_texture), so it is sampled unconditionally:
// no texture() in divergent control flow (implicit derivatives), no unwritten descriptor.
vec4 read_texture_or(uint index, vec2 uv, vec4 fallback)
{
    vec4 texel = texture(u_textures_array[nonuniformEXT(index)], uv);
    return index == 0u ? fallback : texel;
}

GPUMaterial get_material(uint index)
{
    return materials.materials[nonuniformEXT(index)];
}


vec4 get_base_color(in GPUMaterial mat, vec2 uv)
{
    uint base_color_tex_index = mat.textures0.x;
    vec4 base_color = read_texture_or(base_color_tex_index, uv, vec4(1.0));
    return base_color * mat.params0.x;
}


// tangent space normal map texel (no texture: flat, see geometry.frag for the decoding)
vec4 get_normal(in GPUMaterial mat, vec2 uv)
{
    uint normal_tex_index = mat.textures0.y;
    vec4 normal = read_texture_or(normal_tex_index, uv, vec4(0.5, 0.5, 1.0, 1.0));
    return normal;
}



vec4 get_emissive(in GPUMaterial mat, vec2 uv)
{
    uint emissive_color_tex_index = mat.textures0.w;
    vec4 emissive_color = read_texture_or(emissive_color_tex_index, uv, vec4(0.0));
    return emissive_color * mat.params0.y;
}


vec3 get_orm(in GPUMaterial mat, vec2 uv)
{
    uint orm_tex_index = mat.textures0.z;
    vec3 orm_color = read_texture_or(orm_tex_index, uv, vec4(1.0)).rgb;
    vec3 orm_factor = mat.params1.xyz;
    return orm_color * orm_factor;
}
    
    
#endif // PBR_MATERIAL_TABLE