#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "resources/camera.glsl"
#include "resources/pbr_material_table.glsl"
#include "push_constants/model_push_constants.glsl"

// Terrain (material model "terrain", assets/render/schemas/terrain.json): four PBR layers blended by a
// splat map (RGBA = layer weights, uv = the terrain's 0..1) with height based transitions.
//
// Layer textures: base color with the height in alpha, normal map (OpenGL convention: green = up in the
// image), ORM (ao, roughness, metallic). Layers tile in world space (layer_tiling = meters per repeat);
// layers with layer_triplanar != 0 are projected along the three axes (rock on steep slopes).
// Parameters left at 0 in the material fall back to the defaults below.

// ================== INPUTS ==================
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_tangent;
layout(location = 4) in vec3 v_world_bitangent;
layout(location = 5) in vec4 v_curr_clip;
layout(location = 6) in vec4 v_prev_clip;

// ================== OUTPUT ==================
// same g-buffer as geometry.frag: every output as wide as its attachment
layout(location = 0) out vec4 out_g_normal;
layout(location = 1) out vec4 out_g_world_normal;
layout(location = 2) out vec2 out_g_motion_vectors;
layout(location = 3) out vec4 out_g_albedo_roughness;
layout(location = 4) out vec4 out_g_position;
layout(location = 5) out float out_g_linear_depth;
layout(location = 6) out vec4 out_g_geometry_normal;
layout(location = 7) out vec4 out_g_emissive;

struct LayerSample
{
    vec4 albedo_height;     // linear albedo, height
    vec3 normal;            // world space
    vec3 orm;
};

float or_default(float value, float fallback)
{
    return value == 0.0 ? fallback : value;
}

vec4 or_default(vec4 value, vec4 fallback)
{
    return value == vec4(0.0) ? fallback : value;
}

// tangent space normal texel -> normal with scaled bumps (z >= 0)
vec3 decode_normal(vec4 texel, float strength)
{
    vec3 n = texel.xyz * 2.0 - 1.0;
    n.xy *= strength;
    return normalize(vec3(n.xy, max(n.z, 1e-3)));
}

// World space layers tiling along x / z. Tangent frame from the surface normal: T = +x, B = -z on flat ground
// (uv.v grows along +z, the green of an OpenGL normal map points to -v).
LayerSample sample_planar(uvec3 textures, vec3 Ng, float tiling, float normal_strength)
{
    vec2 uv = v_world_pos.xz / tiling;
    LayerSample s;
    s.albedo_height = read_texture_or(textures.x, uv, vec4(0.5, 0.5, 0.5, 0.5));
    s.orm = read_texture_or(textures.z, uv, vec4(1.0)).rgb;

    vec3 n = decode_normal(read_texture_or(textures.y, uv, vec4(0.5, 0.5, 1.0, 1.0)), normal_strength);
    vec3 T = normalize(vec3(1.0, 0.0, 0.0) - Ng * Ng.x);
    vec3 B = cross(Ng, T);
    s.normal = normalize(T * n.x + B * n.y + Ng * n.z);
    return s;
}

// Projected along x, y and z, blended by the normal (whiteout blend of the normal maps).
// The side projections keep "up" in the image pointing to +y.
LayerSample sample_triplanar(uvec3 textures, vec3 Ng, float tiling, float normal_strength, float sharpness)
{
    vec3 w = pow(abs(Ng), vec3(sharpness));
    w /= (w.x + w.y + w.z);

    vec3 p = v_world_pos / tiling;
    vec2 uv_x = vec2(p.z, -p.y);    // tangent x -> +z, y -> +y
    vec2 uv_y = p.xz;               // tangent x -> +x, y -> -z
    vec2 uv_z = vec2(p.x, -p.y);    // tangent x -> +x, y -> +y

    LayerSample s;
    s.albedo_height = read_texture_or(textures.x, uv_x, vec4(0.5)) * w.x
                    + read_texture_or(textures.x, uv_y, vec4(0.5)) * w.y
                    + read_texture_or(textures.x, uv_z, vec4(0.5)) * w.z;
    s.orm = read_texture_or(textures.z, uv_x, vec4(1.0)).rgb * w.x
          + read_texture_or(textures.z, uv_y, vec4(1.0)).rgb * w.y
          + read_texture_or(textures.z, uv_z, vec4(1.0)).rgb * w.z;

    const vec4 flat_normal = vec4(0.5, 0.5, 1.0, 1.0);
    vec3 n_x = decode_normal(read_texture_or(textures.y, uv_x, flat_normal), normal_strength);
    vec3 n_y = decode_normal(read_texture_or(textures.y, uv_y, flat_normal), normal_strength);
    vec3 n_z = decode_normal(read_texture_or(textures.y, uv_z, flat_normal), normal_strength);

    // whiteout: tangent xy plus the surface normal in the projection plane, z scaled by the normal's axis
    vec3 t_x = vec3(n_x.x + Ng.z, n_x.y + Ng.y, abs(n_x.z) * Ng.x);
    vec3 t_y = vec3(n_y.x + Ng.x, -n_y.y + Ng.z, abs(n_y.z) * Ng.y);
    vec3 t_z = vec3(n_z.x + Ng.x, n_z.y + Ng.y, abs(n_z.z) * Ng.z);
    s.normal = normalize(t_x.zyx * w.x + vec3(t_y.x, t_y.z, t_y.y) * w.y + t_z * w.z);
    return s;
}

void main()
{
    GPUMaterial mat = get_material(get_material_index());

    vec3 Ng = normalize(v_world_normal);

    vec4 tiling = mat.params2;
    vec4 height_blend = mat.params3;
    float blend_contrast = max(or_default(mat.params4.x, 0.2), 0.01);
    float macro_scale = or_default(mat.params4.y, 0.23);
    float macro_strength = mat.params4.z;
    float triplanar_sharpness = or_default(mat.params4.w, 4.0);
    vec4 layer_roughness = or_default(mat.params9, vec4(1.0));
    vec4 normal_strength = or_default(mat.params10, vec4(1.0));
    vec4 triplanar = mat.params11;

    uvec3 layer_textures[4] = uvec3[4](mat.textures0.xyz, mat.textures1.xyz, mat.textures2.xyz, mat.textures3.xyz);
    vec4 tints[4] = vec4[4](or_default(mat.params5, vec4(1.0)), or_default(mat.params6, vec4(1.0)),
                            or_default(mat.params7, vec4(1.0)), or_default(mat.params8, vec4(1.0)));

    // no splat map: layer 0
    vec4 weights = read_texture_or(mat.textures1.w, v_uv, vec4(1.0, 0.0, 0.0, 0.0));
    weights /= max(dot(weights, vec4(1.0)), 1e-4);

    float view_depth = -(camera_ubo.view * vec4(v_world_pos, 1.0)).z;
    float macro_blend = macro_strength * smoothstep(8.0, 60.0, view_depth);

    LayerSample layers[4];
    vec4 heights;
    for (int i = 0; i < 4; ++i)
    {
        float layer_tiling = or_default(tiling[i], 4.0);
        if (triplanar[i] != 0.0)
            layers[i] = sample_triplanar(layer_textures[i], Ng, layer_tiling, normal_strength[i], triplanar_sharpness);
        else
            layers[i] = sample_planar(layer_textures[i], Ng, layer_tiling, normal_strength[i]);

        // far away the layer is mixed with itself at a larger scale: breaks up the repetition
        vec2 macro_uv = v_world_pos.xz / layer_tiling * macro_scale;
        vec3 macro = read_texture_or(layer_textures[i].x, macro_uv, layers[i].albedo_height).rgb;
        layers[i].albedo_height.rgb = mix(layers[i].albedo_height.rgb, 0.5 * (layers[i].albedo_height.rgb + macro), macro_blend);

        heights[i] = layers[i].albedo_height.a;
    }

    // height blend: where the weights are close the higher texels win (stones stick out of sand); a layer
    // with no weight never shows
    vec4 keys = weights + heights * height_blend * min(weights * 4.0, vec4(1.0));
    float top = max(max(keys.x, keys.y), max(keys.z, keys.w));
    vec4 blend = max(keys - (top - blend_contrast), vec4(0.0));
    blend /= max(dot(blend, vec4(1.0)), 1e-4);

    vec3 albedo = vec3(0.0);
    vec3 normal_sum = vec3(0.0);
    vec3 orm = vec3(0.0);
    for (int i = 0; i < 4; ++i)
    {
        vec3 layer_albedo = pow(layers[i].albedo_height.rgb, vec3(2.2)) * tints[i].rgb;
        albedo += layer_albedo * blend[i];
        normal_sum += layers[i].normal * blend[i];
        orm += vec3(layers[i].orm.r, layers[i].orm.g * layer_roughness[i], layers[i].orm.b) * blend[i];
    }
    albedo *= or_default(mat.params0.x, 1.0);
    vec3 N = normalize(normal_sum);
    if (dot(N, N) < 0.5)
        N = Ng;

    float ao = mix(1.0, orm.r, clamp(or_default(mat.params1.z, 1.0), 0.0, 1.0));
    float roughness = clamp(orm.g * or_default(mat.params1.x, 1.0), 0.04, 1.0);
    float metallic = clamp(orm.b * mat.params1.y, 0.0, 1.0);
    if ((get_debug_index() & GEOMETRY_DEBUG_GLOSSY) != 0u)
        roughness *= 0.25;

    // ---- outputs (see geometry.frag) ----
    vec3 N_view = normalize((camera_ubo.view * vec4(N, 0.0)).xyz);
    out_g_normal = vec4(N_view * 0.5 + 0.5, roughness);
    out_g_world_normal = vec4(N * 0.5 + 0.5, ao);

    vec2 curr_ndc = v_curr_clip.xy / v_curr_clip.w;
    vec2 prev_ndc = v_prev_clip.xy / v_prev_clip.w;
    out_g_motion_vectors = (curr_ndc * 0.5 + 0.5) - (prev_ndc * 0.5 + 0.5);

    out_g_albedo_roughness = vec4(albedo, roughness);
    out_g_position = vec4(v_world_pos, 1.0);
    out_g_linear_depth = view_depth;
    out_g_geometry_normal = vec4(Ng * 0.5 + 0.5, 1.0);
    out_g_emissive = vec4(0.0, 0.0, 0.0, metallic);
}
