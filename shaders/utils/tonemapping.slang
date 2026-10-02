#pragma once

float3 ACES(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

float3 Uncharted2Tonemap(float3 x)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 tonemap(float3 color)
{
    float exposure = 2.0f;
    color *= exposure;

    float3 mapped = Uncharted2Tonemap(color);

    const float W = 11.2f;
    float3 whiteScale = 1.0f / Uncharted2Tonemap(float3(W, W, W));
    mapped *= whiteScale;

    return mapped;
}

// ---- tone curves of the post processing (tonemap.slang, PostProcessUBO::tone.y). Input: exposed linear
// Rec.709 HDR, output: linear display values 0..1 (the display gamma comes after) ----

// keep in sync with Tonemapper (src/game/post_process.ixx)
static const uint TONEMAPPER_NONE        = 0;
static const uint TONEMAPPER_REINHARD    = 1;
static const uint TONEMAPPER_ACES_FAST   = 2;
static const uint TONEMAPPER_ACES        = 3;
static const uint TONEMAPPER_FILMIC      = 4;
static const uint TONEMAPPER_AGX         = 5;
static const uint TONEMAPPER_AGX_PUNCHY  = 6;
static const uint TONEMAPPER_NEUTRAL     = 7;

float3 reinhard(float3 x)
{
    return x / (1.0 + x);
}

// Uncharted 2 filmic curve (Hable) with its exposure bias 2 and white point 11.2
float3 filmic_hable(float3 x)
{
    const float W = 11.2f;
    return Uncharted2Tonemap(x * 2.0f) / Uncharted2Tonemap(float3(W, W, W));
}

// ACES RRT + ODT fit (Stephen Hill, BakingLab): sRGB -> AP1 (with the RRT desaturation), the fitted
// curve, back to sRGB. Darker midtones than the fast fit (middle gray 0.18 -> ~0.1)
float3 aces_hill(float3 color)
{
    const float3x3 input_mat = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777);
    const float3x3 output_mat = float3x3(
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602);
    color = mul(input_mat, color);
    const float3 a = color * (color + 0.0245786f) - 0.000090537f;
    const float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
    color = a / b;
    return saturate(mul(output_mat, color));
}

// AgX (Troy Sobotka), the polynomial fit of the base contrast (Benjamin Wrensch). Hues of very bright
// colors go to white instead of skewing (ACES turns saturated blue lights purple, orange fire yellow)
float3 agx_contrast_approx(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 agx(float3 color, bool punchy)
{
    // rows of the GLSL (column major) matrices of the reference: mul(v, M)
    const float3x3 agx_mat = float3x3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 agx_mat_inv = float3x3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float min_ev = -12.47393f;
    const float max_ev = 4.026069f;

    color = mul(max(color, 1e-10), agx_mat);
    color = clamp(log2(max(color, 1e-10)), min_ev, max_ev);
    color = (color - min_ev) / (max_ev - min_ev);
    color = agx_contrast_approx(color);

    if (punchy)
    {
        // the "punchy" look of the reference: more contrast and saturation
        const float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
        color = pow(max(color, 0.0), float3(1.35));
        color = luma + 1.4 * (color - luma);
    }

    // the curve ends in display (2.2 gamma) values: back to linear, the display gamma comes later
    color = mul(color, agx_mat_inv);
    return pow(saturate(color), float3(2.2));
}

// Khronos PBR Neutral: base colors stay as they are up to ~0.8, only the highlights are compressed
float3 pbr_neutral(float3 color)
{
    const float start_compression = 0.8 - 0.04;
    const float desaturation = 0.15;

    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    const float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression)
        return color;

    const float d = 1.0 - start_compression;
    const float new_peak = 1.0 - d * d / (peak + d - start_compression);
    color *= new_peak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return lerp(color, float3(new_peak), g);
}

float3 apply_tonemapper(uint tonemapper, float3 color)
{
    color = max(color, 0.0);
    switch (tonemapper)
    {
        case TONEMAPPER_NONE:       return saturate(color);
        case TONEMAPPER_REINHARD:   return reinhard(color);
        case TONEMAPPER_ACES_FAST:  return ACES(color);
        case TONEMAPPER_ACES:       return aces_hill(color);
        case TONEMAPPER_FILMIC:     return saturate(filmic_hable(color));
        case TONEMAPPER_AGX:        return agx(color, false);
        case TONEMAPPER_AGX_PUNCHY: return agx(color, true);
        case TONEMAPPER_NEUTRAL:    return saturate(pbr_neutral(color));
        default:                    return ACES(color);
    }
}
