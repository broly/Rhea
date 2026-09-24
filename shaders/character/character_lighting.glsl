#ifndef CHARACTER_LIGHTING
#define CHARACTER_LIGHTING

// Deferred shading for the character shading models (see character/shading_models.glsl).
// Requires pbr_helpers.glsl (GGX helpers, PI).

#include "character/shading_models.glsl"

struct CharacterGBuffer
{
    uint shading_model;
    vec3 albedo;
    float roughness;
    float metallic;
    float ao;
    vec3 N;
    vec3 emissive;
    vec3 subsurface_color;
    float scatter;
    vec3 hair_tangent;
};

CharacterGBuffer character_decode_gbuffer(uint shading_model, vec4 albedo_roughness, vec3 N, float ao, vec4 model_data)
{
    CharacterGBuffer g;
    g.shading_model = shading_model;
    g.albedo = albedo_roughness.rgb;
    g.roughness = clamp(albedo_roughness.a, 0.04, 1.0);
    g.metallic = 0.0;
    g.ao = ao;
    g.N = N;
    g.emissive = vec3(0.0);
    g.subsurface_color = vec3(0.0);
    g.scatter = 0.0;
    g.hair_tangent = vec3(0.0);

    if (shading_model == SHADING_MODEL_ID_DEFAULT_LIT)
    {
        g.emissive = model_data.rgb;
        g.metallic = model_data.a;
    }
    else if (shading_model == SHADING_MODEL_ID_SKIN)
    {
        g.subsurface_color = model_data.rgb;
        g.scatter = model_data.a;
    }
    else if (shading_model == SHADING_MODEL_ID_HAIR)
    {
        g.hair_tangent = normalize(model_data.rgb * 2.0 - 1.0);
        g.scatter = model_data.a;
    }
    return g;
}

vec3 character_f0(in CharacterGBuffer g)
{
    // UE: F0 = 0.08 * Specular (0.5 by default), skin ~0.028
    float dielectric_f0 = g.shading_model == SHADING_MODEL_ID_SKIN ? 0.028 : 0.04;
    return mix(vec3(dielectric_f0), g.albedo, g.metallic);
}

vec3 character_specular_ggx(in CharacterGBuffer g, vec3 V, vec3 L)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(g.N, L), 0.0);
    float NdotV = max(dot(g.N, V), 1e-4);

    float D = DistributionGGX(g.N, H, g.roughness);
    float G = GeometrySmith(g.N, V, L, g.roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), character_f0(g));

    return D * G * F / (4.0 * NdotV * max(NdotL, 1e-4));
}

// Kajiya-Kay with two shifted lobes (primary white, secondary tinted), energy scaled
vec3 character_hair_specular(in CharacterGBuffer g, vec3 V, vec3 L)
{
    vec3 H = normalize(V + L);

    float exponent = max(2.0 / max(pow(g.roughness, 4.0), 1e-4) - 2.0, 4.0);

    vec3 T1 = normalize(g.hair_tangent + g.N * -0.08);
    vec3 T2 = normalize(g.hair_tangent + g.N * 0.12);

    float TdotH1 = dot(T1, H);
    float TdotH2 = dot(T2, H);
    float sin1 = sqrt(max(1.0 - TdotH1 * TdotH1, 0.0));
    float sin2 = sqrt(max(1.0 - TdotH2 * TdotH2, 0.0));

    float normalization = (exponent + 2.0) / (2.0 * PI);

    float primary = pow(sin1, exponent) * normalization;
    float secondary = pow(sin2, exponent * 0.5) * normalization * 0.5;

    float fresnel = 0.05 + 0.95 * pow(1.0 - max(dot(H, V), 0.0), 5.0);

    return vec3(primary * fresnel) + g.albedo * secondary * 0.25;
}

// radiance leaving towards V from a light with direction L (towards the light) and radiance Li
vec3 character_eval_light(in CharacterGBuffer g, vec3 V, vec3 L, vec3 Li)
{
    float NdotL_raw = dot(g.N, L);
    float NdotL = max(NdotL_raw, 0.0);

    if (g.shading_model == SHADING_MODEL_ID_HAIR)
    {
        float TdotL = dot(g.hair_tangent, L);
        float kajiya_diffuse = sqrt(max(1.0 - TdotL * TdotL, 0.0));

        // cards are thin: soft wrap instead of a hard terminator, scatter adds transmission
        float wrap = clamp(NdotL_raw * 0.5 + 0.5, 0.0, 1.0);
        float transmission = g.scatter * pow(clamp(-dot(L, V), 0.0, 1.0), 4.0);

        vec3 diffuse = g.albedo / PI * (kajiya_diffuse * wrap + transmission);
        vec3 specular = character_hair_specular(g, V, L) * wrap;
        return (diffuse + specular) * Li;
    }

    vec3 specular = character_specular_ggx(g, V, L) * NdotL;

    if (g.shading_model == SHADING_MODEL_ID_SKIN)
    {
        // wrap lighting tinted by the subsurface color approximates the SSS profile
        const float wrap_amount = 0.5;
        float wrapped = clamp((NdotL_raw + wrap_amount) / (1.0 + wrap_amount), 0.0, 1.0);
        vec3 scatter = g.subsurface_color * g.scatter;
        vec3 diffuse_term = mix(vec3(NdotL), vec3(wrapped), scatter);
        return (g.albedo / PI * diffuse_term + specular) * Li;
    }

    vec3 diffuse = g.albedo * (1.0 - g.metallic) / PI * NdotL;
    return (diffuse + specular) * Li;
}

// Karis, mobile env BRDF approximation
vec3 character_env_brdf_approx(vec3 F0, float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

// gi: irradiance-like signal of the RT GI (same term as legacy: gi * albedo)
vec3 character_eval_indirect(in CharacterGBuffer g, vec3 V, vec3 gi)
{
    float NdotV = max(dot(g.N, V), 1e-4);

    vec3 diffuse = gi * g.albedo * (1.0 - g.metallic);

    // no prefiltered environment in the lighting pass yet: the GI signal stands in for it
    vec3 F0 = g.shading_model == SHADING_MODEL_ID_HAIR ? vec3(0.04) : character_f0(g);
    vec3 specular = gi * character_env_brdf_approx(F0, g.roughness, NdotV);

    return (diffuse + specular) * g.ao;
}

#endif // CHARACTER_LIGHTING
