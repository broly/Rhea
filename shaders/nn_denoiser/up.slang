// up.slang — bilinear upsample 2x + 1x1 projection conv.
// Permutation defines:
//   IN_CH        — input channels (low-res tensor)
//   OUT_CH       — output channels (high-res tensor)

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

static const int IN_SLICES  = (IN_CH + 3) / 4;
static const int OUT_SLICES = (OUT_CH + 3) / 4;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);
    if (p.x >= g_nn_pass_indices.uOutSize.x || p.y >= g_nn_pass_indices.uOutSize.y) return;

    NNBilinearTaps taps = nn_bilinear_taps(p, g_nn_pass_indices.uOutSize / 2);

    float4 upsampled[IN_SLICES];
    [ForceUnroll] 
    for (int s = 0; s < IN_SLICES; ++s)
    {
        int idx = nn_get_act_in(s);
        upsampled[s] = nn_bilinear_apply(
            nn_load_act(idx, taps.q00),
            nn_load_act(idx, taps.q10),
            nn_load_act(idx, taps.q01),
            nn_load_act(idx, taps.q11),
            taps.t);
    }

    float result[OUT_CH];
    nn_pointwise_bias_act<IN_SLICES, OUT_CH, NN_ACT_NONE>(
        upsampled,
        g_nn_pass_indices.uPwIdx,
        g_nn_pass_indices.uBiasIdx,
        result);

    nn_store_packed<OUT_SLICES, OUT_CH>(p, result);
}
