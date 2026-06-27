#pragma once

// =============================================================================
//   - g_nn_pass_indices (UBO) - cirrent pass indices
//   - u_nn_activations[]      - RWTexture2D RGBA16F array, intermediate tensors
//   - u_nn_pw_weights         - StructuredBuffer<float4> (SSBO), pointwise weights
//   - u_nn_dw_weights         - StructuredBuffer<float4> (SSBO), depthwise weights
//   - u_nn_biases             - StructuredBuffer<float4> (SSBO), biases
//   - u_nn_weight_layout      - StructuredBuffer<NNWeightSlot> (SSBO), per-slot
//                               base element offsets + dims into the buffers above
//
// All three weight buffers are flat float4 arrays: every original RGBA texel
// becomes one float4 element, laid out in the exact same row-major order the
// textures used. Each weight "slot" (pointwise/depthwise/bias index that the
// pass-indices reference) maps to a contiguous span starting at a base element
// given by u_nn_weight_layout. This contiguous, index-addressable storage is
// what a later GEMM / cooperative-matrix pass can consume directly.
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
#ifndef BINDING_NN_WEIGHT_LAYOUT
    #error "BINDING_NN_WEIGHT_LAYOUT missing"
    #define BINDING_NN_WEIGHT_LAYOUT 5
#endif

#ifndef NN_NUM_ACTIVATION_SLOTS
    #error "NN_NUM_ACTIVATION_SLOTS missing"
    #define NN_NUM_ACTIVATION_SLOTS 1
#endif
// Note: there is deliberately no NN_NUM_*_TEXTURES here. The weight SSBOs are
// single buffers (descriptorCount == 1); the per-kind slot counts live only on
// the CPU and reach the shader as layout-base offsets via the push constant.

import channel_writer;


#ifdef FRAGMENT_SHADER
    #define fragment_readonly readonly
#else
    #define fragment_readonly
#endif


namespace denoiser
{
    public struct NNPassIndicesSSBO
    {
        int2 _pad0;
        int2 _pad1;
    
        public int4 uActIn [16];
        public int4 uActOut[16];
        public int4 uActRes[16];
    
        public int  uPwIdx;
        public int  uDwIdx;
        public int  uBiasIdx;
        int  _pad2;
    
        public int  uBaselineFlatCh;
        public int  uAlbedoFlatCh;
        int  _pad3;
        int  _pad4;
    };
    
    [[vk::binding(BINDING_NN_PASS_INDICES, SET_NN_DENOISER)]]
    public StructuredBuffer<NNPassIndicesSSBO> u_nn_pass_indices;


    public struct NNPassPC
    {
        int2 out_size;
        uint32_t pass_idx;
        int nn_layout_dw_base;
        int nn_layout_bias_base;
    };
    
    [[vk::push_constant]]
    public NNPassPC nn_pass_pc;
    
    public int get_pass_index()
    {
        return nn_pass_pc.pass_idx;
    }
    
    public int2 get_nn_out_size()
    {
        return nn_pass_pc.out_size;
    };
    
    public int get_nn_pointwise_index()
    {
        return u_nn_pass_indices[nn_pass_pc.pass_idx].uPwIdx;
    }
    
    public int get_nn_depthwise_index()
    {
        return u_nn_pass_indices[nn_pass_pc.pass_idx].uDwIdx;
    };
    
    public int get_nn_bias_index()
    {
        return u_nn_pass_indices[nn_pass_pc.pass_idx].uBiasIdx;
    }
    
    // -----------------------------------------------------------------------------
    // Resource arrays
    // -----------------------------------------------------------------------------
    
    [[vk::binding(BINDING_NN_ACTIVATIONS, SET_NN_DENOISER)]]
    public fragment_readonly RWTexture2D<float4> u_nn_activations[NN_NUM_ACTIVATION_SLOTS];

    // -------------------------------------------------------------------------
    // Weight storage (SSBO).
    //
    // Each buffer is a flat array of float4 elements. A weight "slot" occupies a
    // contiguous span; u_nn_weight_layout[slot] gives the base element index and
    // the original 2D/array dimensions so element offsets can be recomputed the
    // same way the texture .Load() coordinates did.
    //
    //   pointwise slot S : base = pw[S].base, element(ic_div4, oc) =
    //                      base + oc * pw[S].width + ic_div4
    //   depthwise slot S : base = dw[S].base, element(kx,ky,ic_div4) =
    //                      base + ic_div4 * (width*height) + (ky+1)*width + (kx+1)
    //                      (width == height == 3)
    //   bias slot S      : base = bs[S].base, element(oc>>2) = base + (oc>>2)
    // -------------------------------------------------------------------------
    public struct NNWeightSlot
    {
        public int base;      // base element index (in float4 units) into the buffer
        public int width;     // texels per row of the source texture
        public int height;    // rows of the source texture
        public int depth;     // array layers (1 for non-array)
    };

    [[vk::binding(BINDING_NN_PW_WEIGHTS, SET_NN_DENOISER)]]
    public StructuredBuffer<float4> u_nn_pw_weights;

    [[vk::binding(BINDING_NN_DW_WEIGHTS, SET_NN_DENOISER)]]
    public StructuredBuffer<float4> u_nn_dw_weights;

    [[vk::binding(BINDING_NN_BIASES, SET_NN_DENOISER)]]
    public StructuredBuffer<float4> u_nn_biases;

    // Layout table for all three weight kinds, indexed by slot. The kinds are
    // disjoint slot-index spaces (pw slot 0, dw slot 0, bias slot 0 are
    // different entries), so they are stored in three sub-ranges. The CPU side
    // packs them as [ pw_slots | dw_slots | bias_slots ] and the dw/bias base
    // offsets reach the shader through the NNPassPC push constant.
    [[vk::binding(BINDING_NN_WEIGHT_LAYOUT, SET_NN_DENOISER)]]
    public StructuredBuffer<NNWeightSlot> u_nn_weight_layout;

    // Sub-range bases inside u_nn_weight_layout, packing order [ pw | dw | bias ].
    // pw starts at 0; dw and bias bases come from the push constant (set on the
    // CPU as num_pw and num_pw+num_dw). This keeps the weight SSBOs single-bound.
    public NNWeightSlot get_pw_slot(int pw_idx)
    {
        return u_nn_weight_layout[pw_idx];
    }
    public NNWeightSlot get_dw_slot(int dw_idx)
    {
        return u_nn_weight_layout[nn_pass_pc.nn_layout_dw_base + dw_idx];
    }
    public NNWeightSlot get_bias_slot(int bias_idx)
    {
        return u_nn_weight_layout[nn_pass_pc.nn_layout_bias_base + bias_idx];
    }
    

     
}
