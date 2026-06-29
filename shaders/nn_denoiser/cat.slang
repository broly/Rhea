// cat.slang — concatenate two tensors A | B by channels
// uActIn[]  — slices A
// uActRes[] — slices B
// uActOut[] — output (a_slices + b_slices)
//
// Permutation defines:
//   A_CHANNELS — channels of A (supposed multiple of 4)
//   B_CHANNELS — channels of B

import nn_denoiser_resource;
import nn_lib;

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
    int2 out_size = denoiser.get_nn_out_size();
    if (p.x >= out_size.x || p.y >= out_size.y) 
        return;

    [ForceUnroll]
    for (int s = 0; s < A_SLICES; ++s)
    {
        NN::store_activation(
            NN::get_activation_output(s),
            p,
            NN::load_activation(
                NN::get_activation_input(s), 
                p
            )
        );
    }
    [ForceUnroll]
    for (int s = 0; s < B_SLICES; ++s)
    {
        NN::store_activation(
            NN::get_activation_output(A_SLICES + s),
            p,
            NN::load_activation(
                NN::get_activation_residual(s), 
                p
            )
        );
    }
    
}
