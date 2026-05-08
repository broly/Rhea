// _separable_conv_body.slang — general body for enc/dec/bottleneck passes.
// SEMANTIC + PERMUTATIONS
// Permutation defines:
//   IN_CH        — input channels
//   OUT_CH       — output channels
//   ACT          — NN_ACT_GELU=1 | NN_ACT_RELU=2 | NN_ACT_NONE=0
//   ADD_RESIDUAL — 0 | 1
#pragma once

#include "nn_denoiser.slang"
#include "nn_lib.slang"

#ifndef IN_CH
    #error "IN_CH define missing"
    #define IN_CH 0
#endif
#ifndef OUT_CH
    #error "OUT_CH define missing"
    #define OUT_CH 0
#endif
#ifndef ACT
    #define ACT NN_ACT_GELU
#endif
#ifndef ADD_RESIDUAL
    #define ADD_RESIDUAL 0
#endif

static const int SEPCONV_IN_SLICES  = (IN_CH + 3) / 4;
static const int SEPCONV_OUT_SLICES = (OUT_CH + 3) / 4;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);
    if (p.x >= g_nn_pass_indices.uOutSize.x || p.y >= g_nn_pass_indices.uOutSize.y) return;

    // Depthwise 3x3
    float4 dwAccum[SEPCONV_IN_SLICES];
    nn_depthwise_3x3<SEPCONV_IN_SLICES>(
        p, g_nn_pass_indices.uOutSize,
        g_nn_pass_indices.uDwIdx,
        dwAccum);

    // Pointwise + bias + activation
    float result[OUT_CH];
    nn_pointwise_bias_act<SEPCONV_IN_SLICES, OUT_CH, ACT>(
        dwAccum,
        g_nn_pass_indices.uPwIdx,
        g_nn_pass_indices.uBiasIdx,
        result);

#if ADD_RESIDUAL
    nn_add_residual<SEPCONV_OUT_SLICES, OUT_CH>(p, result);
#endif

    nn_store_packed<SEPCONV_OUT_SLICES, OUT_CH>(p, result);
}
