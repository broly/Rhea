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




#ifdef FRAGMENT_SHADER
    #define FRAG_READ_ONLY readonly
#else
    #define FRAG_READ_ONLY
#endif


namespace denoiser
{
    struct NNPassIndicesSSBO
    {
        int2 _pad0;
        int2 _pad1;
    
        int4 uActIn [16];
        int4 uActOut[16];
        int4 uActRes[16];
    
        int  uPwIdx;
        int  uDwIdx;
        int  uBiasIdx;
        int  _pad2;
    
        // Head-only: flat-channel indices into input_packed for the AOVs
        // the head pass needs to reconstruct (baseline and albedo). Encoded
        // as a flat channel index because the AOV's RGB triple may STRADDLE
        // two slices of input_packed; the shader decodes
        //   slot_for_ch     = uActRes[ch / 4]
        //   component_in_v4 = ch % 4
        // for each of the three RGB channels and assembles componentwise.
        //
        // -1 in non-head passes (and ignored by their shaders).
        int  uBaselineFlatCh;
        int  uAlbedoFlatCh;
        int  _pad3;
        int  _pad4;
    };
    
    [[vk::binding(BINDING_NN_PASS_INDICES, SET_NN_DENOISER)]]
    StructuredBuffer<NNPassIndicesSSBO> u_nn_pass_indices;


    struct NNPassPC
    {
        int2 out_size;
        uint32_t pass_idx;
    };
    
    [[vk::push_constant]]
    NNPassPC nn_pass_pc;
    
    
    int get_pass_index()
    {
        return nn_pass_pc.pass_idx;
    }
    
    
    int2 get_nn_out_size()
    {
        return nn_pass_pc.out_size;
    };
    
    int get_nn_pointwise_index()
    {
        return u_nn_pass_indices[nn_pass_pc.pass_idx].uPwIdx;
    }
    
    int get_nn_depthwise_index()
    {
        return u_nn_pass_indices[nn_pass_pc.pass_idx].uDwIdx;
    };
    
    
    int get_nn_bias_index()
    {
        // BUG FIX: previously returned uPwIdx, which happened to match
        // uBiasIdx in most passes by coincidence (every pw weight has a
        // matching bias in the current model, and slots are issued in
        // parallel). In the general case — e.g. if a pass had a pointwise
        // without a bias, or vice versa — this silently sampled the wrong
        // bias texture.
        return u_nn_pass_indices[nn_pass_pc.pass_idx].uBiasIdx;
    }
    
    // -----------------------------------------------------------------------------
    // Resource arrays
    // -----------------------------------------------------------------------------
    
    [[vk::binding(BINDING_NN_ACTIVATIONS, SET_NN_DENOISER)]]
    FRAG_READ_ONLY RWTexture2D<float4> u_nn_activations[NN_NUM_ACTIVATION_SLOTS];
    
    [[vk::binding(BINDING_NN_PW_WEIGHTS, SET_NN_DENOISER)]]
    Sampler2D u_nn_pw_weights[NN_NUM_PW_TEXTURES];
    
    [[vk::binding(BINDING_NN_DW_WEIGHTS, SET_NN_DENOISER)]]
    Sampler2DArray u_nn_dw_weights[NN_NUM_DW_TEXTURES];
    
    [[vk::binding(BINDING_NN_BIASES, SET_NN_DENOISER)]]
    Sampler2D u_nn_biases[NN_NUM_BIAS_TEXTURES];

}
