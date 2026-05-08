// head.slang — final head pass: 1x1 conv -> +baseline -> expm1 -> remodulate.
// Out_ch = 3 (RGB).
// uActIn[]   — feature input slices
// uActRes[0] — baseline slice (input_packed slice with log-irradiance)
// uActRes[1] — albedo slice
//
// Permutation defines:
//   IN_CH — input feature channels

#include "nn_denoiser.slang"
#include "nn_lib.slang"
#include "resources/hdr_color_output.slang"
#include "resources/hdr_color_storage.slang"

#ifndef IN_CH
    #error "IN_CH define missing"
    #define IN_CH 0
#endif

static const int IN_SLICES = (IN_CH + 3) / 4;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);
    if (p.x >= g_nn_pass_indices.uOutSize.x || p.y >= g_nn_pass_indices.uOutSize.y) return;

    float4 feat[IN_SLICES];
    
    [ForceUnroll] 
    for (int s = 0; s < IN_SLICES; ++s)
    {
        feat[s] = nn_load_act(nn_get_act_in(s), p);
    }

    // 1x1 conv to RGB (out_ch = 3, not multiplies 4 -> separate inline loop)
    float3 residual;
    [ForceUnroll] 
    for (int oc = 0; oc < 3; ++oc)
    {
        float acc = nn_load_bias(g_nn_pass_indices.uBiasIdx, oc);
        [ForceUnroll] 
        for (int s = 0; s < IN_SLICES; ++s)
        {
            float4 w = nn_load_pw(g_nn_pass_indices.uPwIdx, s, oc);
            acc += dot(w, feat[s]);
        }
        if      (oc == 0) residual.r = acc;
        else if (oc == 1) residual.g = acc;
        else              residual.b = acc;
    }

    float3 baseline = nn_load_act(nn_get_act_res(0), p).rgb;
    float3 logIrr   = baseline + residual;
    float3 irr      = nn_log_expand(logIrr);

    float3 albedo   = nn_load_act(nn_get_act_res(1), p).rgb;
    float3 finalRGB = nn_remodulate(irr, albedo);
   

    u_hdr_color_present_storage[COLOR_OUTPUT_HDR::RTXGI_NEURAL_DENOISED][p]  = float4(finalRGB, 1.0);
    
}
