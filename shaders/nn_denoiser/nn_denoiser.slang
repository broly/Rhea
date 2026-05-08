#pragma once

// =============================================================================
//   - g_nn_pass_indices (UBO) - cirrent pass indices
//   - u_nn_activations[]      - RWTexture2D RGBA16F array, intermediate tensors
//   - u_nn_pw_weights[]       - Sampler2D, pointwise weights
//   - u_nn_dw_weights[]       - Sampler2DArray, depthwise weights
//   - u_nn_biases[]           - biases
// =============================================================================

#ifndef SET_NN_DENOISER
    #error "SET_NN_DENOISER missing — provide 'nn_denoiser' resource"
    #define SET_NN_DENOISER 0
#endif

#ifndef BINDING_NN_PASS_INDICES
    #error "BINDING_NN_PASS_INDICES missing"
    #define BINDING_NN_PASS_INDICES 0
#endif
#ifndef BINDING_NN_ACTIVATIONS
    #error "BINDING_NN_ACTIVATIONS missing"
    #define BINDING_NN_ACTIVATIONS 1
#endif
#ifndef BINDING_NN_PW_WEIGHTS
    #error "BINDING_NN_PW_WEIGHTS missing"
    #define BINDING_NN_PW_WEIGHTS 2
#endif
#ifndef BINDING_NN_DW_WEIGHTS
    #error "BINDING_NN_DW_WEIGHTS missing"
    #define BINDING_NN_DW_WEIGHTS 3
#endif
#ifndef BINDING_NN_BIASES
    #error "BINDING_NN_BIASES missing"
    #define BINDING_NN_BIASES 4
#endif

#ifndef NN_NUM_ACTIVATION_SLOTS
    #error "NN_NUM_ACTIVATION_SLOTS missing"
    #define NN_NUM_ACTIVATION_SLOTS 1
#endif
#ifndef NN_NUM_PW_TEXTURES
    #error "NN_NUM_PW_TEXTURES missing"
    #define NN_NUM_PW_TEXTURES 1
#endif
#ifndef NN_NUM_DW_TEXTURES
    #error "NN_NUM_DW_TEXTURES missing"
    #define NN_NUM_DW_TEXTURES 1
#endif
#ifndef NN_NUM_BIAS_TEXTURES
    #error "NN_NUM_BIAS_TEXTURES missing"
    #define NN_NUM_BIAS_TEXTURES 1
#endif


// Changes every dispatch
struct NNPassIndicesUBO
{
    int2 uOutSize;
    int2 _pad0;

    int4 uActIn [16];
    int4 uActOut[16];
    int4 uActRes[16];

    int  uPwIdx;
    int  uDwIdx;
    int  uBiasIdx;
    int  _pad1;
};

[[vk::binding(BINDING_NN_PASS_INDICES, SET_NN_DENOISER)]]
ConstantBuffer<NNPassIndicesUBO> g_nn_pass_indices;


// -----------------------------------------------------------------------------
// Resource arrays
// -----------------------------------------------------------------------------

[[vk::binding(BINDING_NN_ACTIVATIONS, SET_NN_DENOISER)]]
RWTexture2D<float4> u_nn_activations[NN_NUM_ACTIVATION_SLOTS];

[[vk::binding(BINDING_NN_PW_WEIGHTS, SET_NN_DENOISER)]]
Sampler2D u_nn_pw_weights[NN_NUM_PW_TEXTURES];

[[vk::binding(BINDING_NN_DW_WEIGHTS, SET_NN_DENOISER)]]
Sampler2DArray u_nn_dw_weights[NN_NUM_DW_TEXTURES];

[[vk::binding(BINDING_NN_BIASES, SET_NN_DENOISER)]]
Sampler2D u_nn_biases[NN_NUM_BIAS_TEXTURES];
