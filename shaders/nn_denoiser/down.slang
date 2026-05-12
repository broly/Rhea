// down.slang — 2x2 average pool.
// Permutation defines:
//   CHANNELS — number of channels (must be multiple of 4)

#include "nn_denoiser.slang"
#include "nn_lib.slang"

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

    [ForceUnroll] for (int s = 0; s < N_SLICES; ++s)
    {
        nn_store_act(
            nn_get_act_out(s),
            p,
            nn_avg_pool_2x2(nn_get_act_in(s), p)
        );
    }
    
    
}
