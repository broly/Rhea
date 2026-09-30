#ifndef FOLIAGE_MATERIAL
#define FOLIAGE_MATERIAL

// The "foliage" material model (trees: bark and leaves), see assets/render/schemas/foliage.json for the
// parameter -> GPUMaterial mapping. Requires resources/pbr_material_table.glsl. Fragment shaders only.
//
// GPUMaterial usage:
//   params0   x base_color_factor  y emissive_factor (unused)  z opacity_clip  w normal_strength
//   params1   x roughness_factor   y metallic_factor  z occlusion_factor
//   params2   transmission: light through a leaf = albedo x rgb, a = strength (0: bark)
//   textures0 base color (a = opacity of masked materials), normal (OpenGL convention: green up), orm

// Opacity of a masked material, compared with opacity_clip. The mips average the alpha of a leaf with the
// empty texels around it: far leaves would fall below the clip value and the crown thin out. Alpha is scaled
// up with the mip level to keep the coverage about constant.
float foliage_opacity(in GPUMaterial mat, vec2 uv)
{
    float alpha = read_texture_or(mat.textures0.x, uv, vec4(1.0)).a;
    float lod = textureQueryLod(u_textures_array[nonuniformEXT(mat.textures0.x)], uv).x;
    return alpha * (1.0 + max(lod, 0.0) * 0.25);
}

// Tangent space normal
vec3 foliage_normal_ts(in GPUMaterial mat, vec2 uv)
{
    vec3 n = get_normal(mat, uv).xyz * 2.0 - 1.0;
    n.xy *= mat.params0.w;
    return normalize(n);
}

#endif // FOLIAGE_MATERIAL
