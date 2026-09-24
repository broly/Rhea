#ifndef CHARACTER_MATERIAL
#define CHARACTER_MATERIAL

// Port of the CosmoBunny UE master materials (M_CB_*), see assets/render/schemas/character.json
// for the parameter -> GPUMaterial mapping. Requires resources/pbr_material_table.glsl.
//
// GPUMaterial usage:
//   params0   x base_color_factor  y emissive_factor  z opacity_clip  w normal_strength
//   params1   x roughness_factor   y metallic_factor  z occlusion_factor  w specular
//   params2-4 tint 01 / 02 / 03 (rgb)
//   params5-6 tint mask channel selectors a / b
//   params7   emissive color
//   params8   subsurface color (rgb), scatter (w)
//   params9   hair: brightness, saturate, noise tiling, edge mask min
//   params10  hair: edge mask contrast, root/tip blend, mask mip bias
//   params11  hair root color, params12 hair tip color
//   params13  hair tangent A, params14 hair tangent B, params15 hair tangent
//   textures0 base color, normal, orm, emissive
//   textures1 tint mask, hair mask (r alpha, g depth, b id, a root), hair noise

#ifndef SHADING_MODEL_DEFAULT_LIT
    #define SHADING_MODEL_DEFAULT_LIT 1
#endif
#ifndef SHADING_MODEL_SKIN
    #define SHADING_MODEL_SKIN 0
#endif
#ifndef SHADING_MODEL_HAIR
    #define SHADING_MODEL_HAIR 0
#endif
#ifndef USE_EMISSIVE
    #define USE_EMISSIVE 0
#endif
#ifndef USE_TINT_MASK
    #define USE_TINT_MASK 0
#endif
#ifndef USE_HAIR_MASK
    #define USE_HAIR_MASK 0
#endif
#ifndef USE_HAIR_NOISE
    #define USE_HAIR_NOISE 0
#endif


vec3 cm_srgb_to_linear(vec3 c)
{
    // same approximation as the rest of the engine (geometry.frag)
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec4 cm_sample(uint index, vec2 uv)
{
    return texture(u_textures_array[nonuniformEXT(index)], uv);
}

vec4 cm_sample_bias(uint index, vec2 uv, float bias)
{
    return texture(u_textures_array[nonuniformEXT(index)], uv, bias);
}

// raw (sRGB encoded) base color texture, alpha is the opacity mask for masked materials
vec4 cm_base_color_texture(in GPUMaterial mat, vec2 uv)
{
#if USE_BASE_COLOR
    return cm_sample(mat.textures0.x, uv);
#else
    return vec4(1.0);
#endif
}

// lerp(lerp(01, 03, mask . select_a), 02, mask . select_b)  (M_CB_ARM/DRESS/..., M_CB_EYES)
vec3 cm_tint(in GPUMaterial mat, vec2 uv)
{
#if USE_TINT_MASK
    vec4 mask = cm_sample(mat.textures1.x, uv);
    float a = clamp(dot(mask, mat.params5), 0.0, 1.0);
    float b = clamp(dot(mask, mat.params6), 0.0, 1.0);
    return mix(mix(mat.params2.rgb, mat.params4.rgb, a), mat.params3.rgb, b);
#else
    return mat.params2.rgb;
#endif
}

vec4 cm_hair_mask(in GPUMaterial mat, vec2 uv)
{
#if USE_HAIR_MASK
    // M_CB_Hair_Master_01 samples its masks with MipBias
    return cm_sample_bias(mat.textures1.y, uv, mat.params10.z);
#else
    return vec4(1.0, 0.0, 0.0, 0.0);
#endif
}

// UE CheapContrast
float cm_cheap_contrast(float value, float contrast)
{
    return clamp(mix(-contrast, 1.0 + contrast, value), 0.0, 1.0);
}

// V: direction to the viewer (or to the light in shadow passes), Nv: vertex normal
float character_opacity(in GPUMaterial mat, vec2 uv, vec3 V, vec3 Nv)
{
#if SHADING_MODEL_HAIR
    vec4 mask = cm_hair_mask(mat, uv);
    float alpha = mask.r;
    float depth = mask.g;

    float edge_contrast = mat.params10.x;
    float edge_mask_min = mat.params9.w;

    float facing = abs(dot(V, Nv)) * 1.5;
    float edge = mix(edge_mask_min, 1.0, cm_cheap_contrast(facing, edge_contrast));

    return mix(depth * depth, 1.0, edge) * alpha;
#else
    return cm_base_color_texture(mat, uv).a;
#endif
}

vec3 character_hair_base_color(in GPUMaterial mat, vec2 uv)
{
    float brightness = mat.params9.x;
    float saturation = mat.params9.y;
    float noise_tiling = mat.params9.z;
    float root_tip = mat.params10.y;

    vec3 root_tip_color = mix(mat.params11.rgb, mat.params12.rgb, root_tip);

    // UseDiffuseBaseColor = false: lerp(Root, Tip) + Diffuse
    vec3 color = root_tip_color + cm_srgb_to_linear(cm_base_color_texture(mat, uv).rgb);

    // Desaturation(Fraction = 1 - Saturate)
    float luminance = dot(color, vec3(0.3, 0.59, 0.11));
    color = mix(color, vec3(luminance), 1.0 - saturation);

#if USE_HAIR_NOISE
    color *= cm_sample(mat.textures1.z, uv * noise_tiling).r;
#endif

    return clamp(color * brightness, 0.0, 1.0);
}

vec3 character_base_color(in GPUMaterial mat, vec2 uv)
{
#if SHADING_MODEL_HAIR
    return character_hair_base_color(mat, uv) * mat.params0.x;
#else
    vec3 base = cm_srgb_to_linear(cm_base_color_texture(mat, uv).rgb);
    return base * cm_tint(mat, uv) * mat.params0.x;
#endif
}

// ao, roughness, metallic (textures are linear, see tools/ue_import)
vec3 character_orm(in GPUMaterial mat, vec2 uv)
{
#if USE_ORM
    vec3 orm = cm_sample(mat.textures0.z, uv).rgb;
#else
    vec3 orm = vec3(1.0, 1.0, 0.0);
#endif
    // occlusion is lerped towards 1 (UE style strength), roughness/metallic are scaled
    float ao = mix(1.0, orm.r, mat.params1.z);
    return vec3(ao, orm.g * mat.params1.x, orm.b * mat.params1.y);
}

vec3 character_emissive(in GPUMaterial mat, vec2 uv)
{
#if USE_EMISSIVE
    return cm_srgb_to_linear(cm_sample(mat.textures0.w, uv).rgb) * mat.params7.rgb * mat.params0.y;
#else
    return vec3(0.0);
#endif
}

// tangent space normal (+Y is the engine bitangent direction)
vec3 character_normal_ts(in GPUMaterial mat, vec2 uv)
{
#if USE_NORMAL_MAP
    vec3 n = cm_sample(mat.textures0.y, uv).rgb;
    // same convention as geometry.frag (textures are stored as glTF / OpenGL normal maps)
    n.y = 1.0 - n.y;
    n = n * 2.0 - 1.0;
    n.xy *= mat.params0.w;
    return normalize(n);
#else
    return vec3(0.0, 0.0, 1.0);
#endif
}

// hair: UE "Normal" pin of the hair shading model is the strand tangent (tangent space)
vec3 character_hair_tangent_ts(in GPUMaterial mat, vec2 uv)
{
    float id = cm_hair_mask(mat, uv).b;
    return normalize(mix(mat.params13.xyz, mat.params14.xyz, id) + mat.params15.xyz);
}

#endif // CHARACTER_MATERIAL
