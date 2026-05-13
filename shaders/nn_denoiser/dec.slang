// decoder ConvBlock pass.
// SeparableConv 3x3 + bias + activation. Without residual.
import separable_conv;

#include "_choose_activation_fn.slang"
#include "_validate_channels.slang"

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    separable_conv<false, IN_CH, OUT_CH, ActivationFn>(dtid);
}