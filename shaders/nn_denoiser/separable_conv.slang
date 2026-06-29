// Scalar separable conv: depthwise 3x3 -> pointwise 1x1 -> bias -> act (+ optional residual).
// One thread = one pixel, weights read straight from the SSBO. No groupshared, no
// barriers, so it's dead simple. The coopmat version of the same math lives in
// nn_coopmat.slang and is what actually runs on the tensor cores.

import nn_denoiser_resource;
import nn_lib;

// Heads up: don't rename in_ch/out_ch to IN_CH/OUT_CH here — those are -D defines,
// and Slang's preprocessor would happily turn the param into `int 32`.
namespace NN
{
public void separable_conv_scalar<
    bool add_residual, int in_ch, int out_ch, TAct : NN::ActivationFunctions::IAct>(
    uint3 dtid)
{
    static const int in_slices  = (in_ch + 3) / 4;
    static const int out_slices = (out_ch + 3) / 4;

    int2 p = int2(dtid.xy);
    int2 out_size = denoiser.get_nn_out_size();
    if (p.x >= out_size.x || p.y >= out_size.y)
        return;

    float4 dwAccum[in_slices];
    NN::depthwise_3x3<in_slices>(
        p, out_size, denoiser.get_nn_depthwise_index(), dwAccum);

    float result[out_ch];
    NN::pointwise_bias_activate<in_slices, out_ch, TAct>(
        dwAccum,
        denoiser.get_nn_pointwise_index(),
        denoiser.get_nn_bias_index(),
        result);

    if (add_residual)
        NN::add_residual<out_slices, out_ch>(p, result);
    NN::store_packed<out_slices, out_ch>(p, result);
}
}
