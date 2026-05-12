// head.slang — final head pass: 1x1 conv -> +baseline -> expm1 -> remodulate.
// Out_ch = 3 (RGB).
//
// IMPORTANT: This pass intentionally does NOT recover baseline / albedo via
// nn_get_act_res(s). That mechanism reads slices of input_packed, but the
// values it needs (full RGB log-irradiance and full RGB albedo) are NOT
// aligned on 4-channel boundaries inside input_packed — they are split
// across slices, with .rgb of any single slice corresponding to a mix of
// AOVs (e.g. slice 1 = (normal.g, normal.b, depth, albedo.r)).
//
// Old code did `nn_load_act(nn_get_act_res(1), p).rgb` and got back
// (normal.g, normal.b, depth) instead of albedo — which is exactly the
// "albedo with weird colors" artifact you observed: finalRGB = irr *
// (normal.gb, depth) visually resembles a tinted albedo, but it isn't one.
//
// Fix: sample albedo straight from the gbuffer and the noisy color from
// the HDR present buffer. These are the originals the input pass already
// consumed, so this round-trips through the network correctly without
// touching the export-side input_packed layout.
//
// uActIn[]   — feature input slices (decoder output)
// uActRes[]  — unused now (kept in pipeline.json for compatibility)
//
// Permutation defines:
//   IN_CH — input feature channels

#include "nn_denoiser.slang"
#include "nn_lib.slang"
#include "resources/gbuffer.slang"
#include "resources/hdr_color_output.slang"
#include "resources/hdr_color_storage.slang"

#ifndef IN_CH
    #error "IN_CH define missing"
    #define IN_CH 0
#endif

#ifndef ALBEDO_EPS
    #define ALBEDO_EPS 1000     // 1e-3 * 1e6  (same as input.slang)
#endif
#ifndef CLAMP_MAX
    #define CLAMP_MAX 64
#endif

static const float ALBEDO_EPS_F = float(ALBEDO_EPS) * 1e-6;
static const float CLAMP_MAX_F  = float(CLAMP_MAX);

static const int IN_SLICES = (IN_CH + 3) / 4;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);

    int2 out_size = denoiser.get_nn_out_size();

    if (p.x >= out_size.x || p.y >= out_size.y)
        return;

    float2 uv = (float2(p) + 0.5) / float2(out_size);

    // -----------------------------------------------------------------
    // 1x1 conv: feature slices -> RGB residual.
    // -----------------------------------------------------------------
    float4 feat[IN_SLICES];

    [ForceUnroll]
    for (int s = 0; s < IN_SLICES; ++s)
    {
        feat[s] = nn_load_act(nn_get_act_in(s), p);
    }

    float3 residual;
    [ForceUnroll]
    for (int oc = 0; oc < 3; ++oc)
    {
        float acc = nn_load_bias(denoiser.get_nn_bias_index(), oc);

        [ForceUnroll]
        for (int s = 0; s < IN_SLICES; ++s)
        {
            float4 w = nn_load_pw(denoiser.get_nn_pointwise_index(), s, oc);
            acc += dot(w, feat[s]);
        }
        if (oc == 0)
            residual.r = acc;
        else if (oc == 1)
            residual.g = acc;
        else
            residual.b = acc;
    }

    // -----------------------------------------------------------------
    // Baseline = log-compressed demodulated noisy.
    // Must match input.slang exactly so the residual learned during
    // training composes correctly.
    // -----------------------------------------------------------------
    float3 noisy  = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI, 0>(uv).rgb;
    float3 albedo = nn_load_packed_rgb(get_nn_albedo_flat_ch(), p);
    // float3 albedo = get_gbuffer<GBUFFER_SLOT::GBUFFER_SLOT_ALBEDO_ROUGHNESS, 0>(uv).rgb;

    float3 irr_log_baseline = nn_log_compress(
        nn_demodulate(noisy, albedo, ALBEDO_EPS_F, CLAMP_MAX_F));

    // -----------------------------------------------------------------
    // Apply network residual, undo log compression, remodulate by albedo.
    // -----------------------------------------------------------------
    float3 logIrr   = irr_log_baseline + residual;
    float3 irr      = nn_log_expand(logIrr);
    float3 finalRGB = nn_remodulate(irr, albedo);

    u_hdr_color_present_storage[COLOR_OUTPUT_HDR::RTXGI_NEURAL_DENOISED][p] = float4(finalRGB, 1.0);
}
