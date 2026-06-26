import nn_denoiser_resource;
import nn_lib;


public void separable_conv<
    bool add_residual,
    int in_ch,
    int out_ch,
    TAct: NNActFn::IAct
>(int3 dtid)
{
    static const int sepconv_in_slices  = (in_ch + 3) / 4;
    static const int sepconv_out_slices = (out_ch + 3) / 4;

    int2 p = int2(dtid.xy);
    int2 out_size = denoiser.get_nn_out_size();
    
    if (p.x >= out_size.x || p.y >= out_size.y) 
        return;

    // Depthwise 3x3
    float4 dwAccum[sepconv_in_slices];
    nn_depthwise_3x3<sepconv_in_slices>(
        p, denoiser.get_nn_out_size(),
        denoiser.get_nn_depthwise_index(),
        dwAccum);

    // Pointwise + bias + activation
    float result[out_ch];
    nn_pointwise_bias_act<sepconv_in_slices, out_ch, TAct>(
        dwAccum,
        denoiser.get_nn_pointwise_index(),
        denoiser.get_nn_bias_index(),
        result);

    if (add_residual)
    {    
        nn_add_residual<sepconv_out_slices, out_ch>(p, result);
    }

    nn_store_packed<sepconv_out_slices, out_ch>(p, result);
    
    
}