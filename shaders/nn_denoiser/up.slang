// up.slang — bilinear upsample 2x + 1x1 projection (linear, no activation).
// NN_USE_COOPMAT (0/1) selects the back end:
//   0 -> scalar  (8x8 workgroup)
//   1 -> coopmat (64x1 workgroup, separable_conv_coopmat_upsample)
import nn_denoiser_resource;
import nn_lib;
import separable_conv;

#ifndef NN_USE_COOPMAT
    #define NN_USE_COOPMAT 0
#endif

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

#if NN_USE_COOPMAT
import nn_coopmat_types;
import nn_coopmat;
#include "_coopmat_frag.slang"

static const int CM_K_PAD = IN_SLICES * 4;

groupshared NN::CoopMat::InputType g_Wsh[OUT_CH * CM_K_PAD];
groupshared NN::CoopMat::InputType g_Xsh[CM_K_PAD * 64];
groupshared NN::CoopMat::AccumType g_Ysh[OUT_CH * 64];

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID,
          uint3 gid  : SV_GroupID,
          uint3 gtid : SV_GroupThreadID,
          uint  gidx : SV_GroupIndex)
{
    NN::CoopMat::separable_conv_coopmat_upsample<
        OUT_CH,
        IN_SLICES,
        OUT_SLICES,
        CM_K_PAD,
        NN::ActivationFunctions::None>(gid, gtid, gidx, g_Wsh, g_Xsh, g_Ysh);
}

#else  // ---- scalar path ----

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);
    int2 out_size = denoiser.get_nn_out_size();
    if (p.x >= out_size.x || p.y >= out_size.y)
        return;

    NN::NNBilinearTaps taps = NN::nn_bilinear_taps(p, out_size / 2);
    float4 upsampled[IN_SLICES];
    [ForceUnroll]
    for (int s = 0; s < IN_SLICES; ++s)
    {
        int idx = NN::get_activation_input(s);
        upsampled[s] = NN::nn_bilinear_apply(
            NN::load_activation(idx, taps.q00), NN::load_activation(idx, taps.q10),
            NN::load_activation(idx, taps.q01), NN::load_activation(idx, taps.q11), taps.t);
    }

    float result[OUT_CH];
    NN::pointwise_bias_activate<IN_SLICES, OUT_CH, NN::ActivationFunctions::None>(
        upsampled,
        denoiser.get_nn_pointwise_index(),
        denoiser.get_nn_bias_index(),
        result);

    NN::store_packed<OUT_SLICES, OUT_CH>(p, result);
}

#endif
