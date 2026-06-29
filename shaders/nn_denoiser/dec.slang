// dec.slang — decoder ConvBlock. SeparableConv 3x3 + bias + act. No residual.
// NN_USE_COOPMAT (0/1) numeric variant from the host selects the back end:
//   0 -> scalar path  (8x8 workgroup, separable_conv_scalar)
//   1 -> coopmat path (64x1 workgroup, separable_conv_coopmat)
import separable_conv;

#include "_choose_activation_fn.slang"
#include "_validate_channels.slang"

#ifndef NN_USE_COOPMAT
    #define NN_USE_COOPMAT 0
#endif

#define CM_ADD_RESIDUAL 0

#if NN_USE_COOPMAT
import nn_lib;
import nn_denoiser_resource;
import nn_coopmat_types;
import nn_coopmat;
#include "_coopmat_frag.slang"     // CMatA/B/C + linalg (fragment types in entry scope)

static const int CM_K_PAD     = ((IN_CH + 3) / 4) * 4;
static const int CM_IN_SLICES = (IN_CH + 3) / 4;
static const int CM_OUT_SLICES = (OUT_CH + 3) / 4;

// groupshared stays in the entry, sized exactly for this pass.
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
    NN::CoopMat::separable_conv_coopmat<
        bool(CM_ADD_RESIDUAL),
        OUT_CH,
        CM_IN_SLICES,
        CM_OUT_SLICES,
        CM_K_PAD,
        ActivationFn>(gid, gtid, gidx, g_Wsh, g_Xsh, g_Ysh);
}

#else  // ---- scalar path ----

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    NN::separable_conv_scalar<false, IN_CH, OUT_CH, ActivationFn>(dtid);
}

#endif
