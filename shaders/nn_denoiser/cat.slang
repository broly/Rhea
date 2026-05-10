// cat.slang — concatenate two tensors A | B by channels
// uActIn[]  — slices A
// uActRes[] — slices B
// uActOut[] — output (a_slices + b_slices)
//
// Permutation defines:
//   A_CHANNELS — channels of A (supposed multiple of 4)
//   B_CHANNELS — channels of B

#include "nn_denoiser.slang"
#include "nn_lib.slang"

#ifndef A_CHANNELS
    #error "A_CHANNELS define missing"
    #define A_CHANNELS ((uint)-1)
#endif
#ifndef B_CHANNELS
    #error "B_CHANNELS define missing"
    #define B_CHANNELS ((uint)-1)
#endif

static const int A_SLICES = (A_CHANNELS + 3) / 4;
static const int B_SLICES = (B_CHANNELS + 3) / 4;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);
    int2 out_size = get_nn_out_size();
    if (p.x >= out_size.x || p.y >= out_size.y) 
        return;

    [ForceUnroll]
    for (int s = 0; s < A_SLICES; ++s)
    {
        nn_store_act(
            nn_get_act_out(s),
            p,
            nn_load_act(
                nn_get_act_in(s), 
                p
            )
        );
    }
    [ForceUnroll]
    for (int s = 0; s < B_SLICES; ++s)
    {
        nn_store_act(
            nn_get_act_out(A_SLICES + s),
            p,
            nn_load_act(
                nn_get_act_res(s), 
                p
            )
        );
    }
    
}
