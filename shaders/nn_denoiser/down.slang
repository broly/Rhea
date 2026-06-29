// down.slang — 2x2 average pool.
// Permutation defines:
//   CHANNELS — number of channels (must be multiple of 4)

import nn_denoiser_resource;
import nn_lib;

#ifndef CHANNELS
    #error "CHANNELS define missing"
    #define CHANNELS 0
#endif

static const int N_SLICES = (CHANNELS + 3) / 4;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);
    
    int2 out_size = denoiser.get_nn_out_size();
    
    if (p.x >= out_size.x || p.y >= out_size.y) 
        return;

    [ForceUnroll] 
    for (int s = 0; s < N_SLICES; ++s)
    {
        NN::store_activation(
            NN::get_activation_output(s),
            p,
            NN::avg_pool_2x2(NN::get_activation_input(s), p)
        );
    }
    
    
}
