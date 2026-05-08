// bottleneck.slang — bottleneck ConvBlock pass.
// SeparableConv 3x3 + bias + activation + residual.
#define ADD_RESIDUAL 1
#include "_separable_conv_body.slang"
