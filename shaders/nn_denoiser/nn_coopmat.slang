// Cooperative-matrix version of the pointwise (1x1) conv. Over an 8x8 = 64-pixel
// tile the pointwise stage is just a dense matmul
//     Y[OUT_CH x 64] = W[OUT_CH x K] * X[K x 64]   (+ bias + activation)
// which we stage into groupshared and run on the tensor cores. ~90% of the
// denoiser's MACs live here, so this is the part worth accelerating.
//
// The work is split into phases (block_pre / gemm / block_post) because the GEMM
// needs a GroupMemoryBarrierWithGroupSync() on either side. The entry doesn't see
// any of that though — separable_conv_coopmat() at the bottom bundles the phases
// + barriers into a single call, so an entry main() is one line, same as scalar.

#pragma once

import nn_denoiser_resource;
import nn_lib;
import nn_coopmat_types;
#include "_coopmat_frag.slang"   // using namespace linalg; CMatA/B/C; NN_CM_* macros

namespace NN::CoopMat
{

// Carries the per-tile pixel + bounds from the pre phase to the post phase.
public struct Tile
{
    public int2 pixel;
    public bool in_bounds;
};

// Phase 1: depthwise + stage this thread's X column + cooperatively stage W.
// IN_SLICES = (IN_CH+3)/4, K = padded contraction dim (== IN_SLICES*4).
public Tile block_pre<int IN_SLICES>(
    int OUT_CH_NUM,
    int K, uint3 gid, uint3 gtid, uint  gidx,
    __ref NN::CoopMat::InputType Wsh[],  // use __ref instead inout to handle `groupshared` qualifier
    __ref NN::CoopMat::InputType Xsh[])
{
    Tile tile;
    const int tile_p = int(gtid.x);
    const int lx = tile_p & 7;
    const int ly = tile_p >> 3;
    tile.pixel = int2(int(gid.x) * 8 + lx, int(gid.y) * 8 + ly);

    const int2 outsz = denoiser.get_nn_out_size();
    tile.in_bounds = (tile.pixel.x < outsz.x) && (tile.pixel.y < outsz.y);

    // depthwise 3x3 (per-channel)
    float4 dw[IN_SLICES];
    if (tile.in_bounds)
        NN::depthwise_3x3<IN_SLICES>(tile.pixel, outsz, denoiser.get_nn_depthwise_index(), dw);
    else
    { 
        [ForceUnroll] 
        for (int s = 0; s < IN_SLICES; ++s) 
            dw[s] = float4(0,0,0,0); 
    }

    // stage this thread's X column
    [ForceUnroll]
    for (int s = 0; s < IN_SLICES; ++s)
    {
        int ic = s * 4;
        Xsh[(ic + 0) * NN_CM_TILE_PIXELS + tile_p] = NN::CoopMat::InputType(dw[s].x);
        Xsh[(ic + 1) * NN_CM_TILE_PIXELS + tile_p] = NN::CoopMat::InputType(dw[s].y);
        Xsh[(ic + 2) * NN_CM_TILE_PIXELS + tile_p] = NN::CoopMat::InputType(dw[s].z);
        Xsh[(ic + 3) * NN_CM_TILE_PIXELS + tile_p] = NN::CoopMat::InputType(dw[s].w);
    }

    // stage W[OUT_CH x K] cooperatively
    const int K4     = K / 4;
    const int total4 = OUT_CH_NUM * K4;
    const int pw_idx = denoiser.get_nn_pointwise_index();
    for (uint e = gidx; e < uint(total4); e += 64u)
    {
        int oc      = int(e) / K4;
        int ic_div4 = int(e) % K4;
        float4 w    = NN::load_pointwise(pw_idx, ic_div4, oc);
        int dst     = oc * K + ic_div4 * 4;
        Wsh[dst + 0] = NN::CoopMat::InputType(w.x);
        Wsh[dst + 1] = NN::CoopMat::InputType(w.y);
        Wsh[dst + 2] = NN::CoopMat::InputType(w.z);
        Wsh[dst + 3] = NN::CoopMat::InputType(w.w);
    }

    return tile;
}

/**
 * @brief Phase 2: the actual cooperative-matrix GEMM, c_output = a_weights * b_input.
 * @tparam OUTPUT_CHANNELS_NUM: number of output channels 
 * @tparam K_PAD: Padded K dimension of GEMM
 * @param a_weights: current shared weights
 * @param b_input: input shared data
 * @param c_output: output shared data
 */
public void gemm<int OUTPUT_CHANNELS_NUM, int K_PAD>(
    uint group_index,
    __ref groupshared NN::CoopMat::InputType a_weights[],  // <- float16_t (current shared weights)
    __ref groupshared NN::CoopMat::InputType b_input[],    // <- float16_t (input shared data)
    __ref groupshared NN::CoopMat::AccumType c_output[])   // <- float (output shared data)
{
    // #define NN_CM_M/N/K 16
    const int M_TILES = OUTPUT_CHANNELS_NUM / NN_CM_M;  // Rows
    const int N_TILES = NN_CM_TILE_PIXELS / NN_CM_N;   // 64/16 = 4, Cols
    const int K_TILES = K_PAD / NN_CM_K;  // Z-dim
    const int TILE_COUNT = M_TILES * N_TILES;

    const uint subgroup_size = WaveGetLaneCount();
    const uint subgroup_id = group_index / subgroup_size;
    const uint subgroups_count = max(1u, 64u / subgroup_size);
    
    for (uint t = subgroup_id; t < uint(TILE_COUNT); t += subgroups_count)
    {
        int mt = int(t) / N_TILES;  //  row index
        int nt = int(t) % N_TILES;  // col index
        int row0 = mt * NN_CM_M;
        int col0 = nt * NN_CM_N;

        // typealias CMatC = CoopMat<NN::CoopMat::AccumType, MemoryScope.Subgroup, NN_CM_M, NN_CM_N,  CoopMatMatrixUse.MatrixAccumulator>;
        CMatC acc = CMatC(NN::CoopMat::AccumType(0));
        for (int kt = 0; kt < K_TILES; ++kt)
        {
            int k0 = kt * NN_CM_K;
            // typealias CMatA = CoopMat<NN::CoopMat::InputType, MemoryScope.Subgroup, NN_CM_M, NN_CM_K,  CoopMatMatrixUse.MatrixA>;
            CMatA a = CMatA.Load<CoopMatMatrixLayout.RowMajor>(a_weights, uint(row0 * K_PAD + k0), uint(K_PAD));
            // typealias CMatB = CoopMat<NN::CoopMat::InputType, MemoryScope.Subgroup, NN_CM_K, NN_CM_N, CoopMatMatrixUse.MatrixB>;
            CMatB b = CMatB.Load<CoopMatMatrixLayout.RowMajor>(b_input, uint(k0 * NN_CM_TILE_PIXELS + col0), 
                uint(NN_CM_TILE_PIXELS));
            acc = coopMatMulAdd<NN::CoopMat::AccumType, false>(a, b, acc);
        }
        acc.Store<CoopMatMatrixLayout.RowMajor>(
            c_output, uint(row0 * NN_CM_TILE_PIXELS + col0), uint(NN_CM_TILE_PIXELS));
    }
}

// Phase 3: pull this thread's column back out, add bias, activate, optional
// residual, store. ADD_RESIDUAL is compile-time; OUT_SLICES = (OUT_CH+3)/4.
public void block_post<int OUT_SLICES, int OUT_CH_NUM, bool ADD_RESIDUAL, TAct : NN::ActivationFunctions::IAct>(
    Tile tile,
    uint3 gtid,
    __ref NN::CoopMat::AccumType Ysh[])
{
    if (!tile.in_bounds) return;

    const int tile_p = int(gtid.x);
    const int bias_idx = denoiser.get_nn_bias_index();

    float result[OUT_CH_NUM];
    [ForceUnroll]
    for (int oc = 0; oc < OUT_CH_NUM; ++oc)
    {
        float v = float(Ysh[oc * NN_CM_TILE_PIXELS + tile_p]) + NN::load_bias(bias_idx, oc);
        result[oc] = TAct::apply(v);
    }

    if (ADD_RESIDUAL)
        NN::add_residual<OUT_SLICES, OUT_CH_NUM>(tile.pixel, result);

    NN::store_packed<OUT_SLICES, OUT_CH_NUM>(tile.pixel, result);
}

// Phase 1, upsample flavor (for up.slang): same W/X staging as block_pre, but the
// input column comes from a bilinear 2x upsample instead of a depthwise. up passes
// are a linear projection, so block_post gets ActivationFunctions::None.
public Tile block_pre_upsample<int IN_SLICES>(
    int OUT_CH_NUM,
    int K,
    uint3 gid,
    uint3 gtid,
    uint  gidx,
    __ref NN::CoopMat::InputType Wsh[],
    __ref NN::CoopMat::InputType Xsh[])
{
    Tile tile;
    const int tile_p = int(gtid.x);
    const int lx = tile_p & 7;
    const int ly = tile_p >> 3;
    tile.pixel = int2(int(gid.x) * 8 + lx, int(gid.y) * 8 + ly);

    const int2 outsz = denoiser.get_nn_out_size();
    tile.in_bounds = (tile.pixel.x < outsz.x) && (tile.pixel.y < outsz.y);

    // bilinear upsample of the low-res input tensor (out_size/2 -> out_size)
    NN::NNBilinearTaps taps = NN::nn_bilinear_taps(tile.pixel, outsz / 2);
    float4 up[IN_SLICES];
    if (tile.in_bounds)
    {
        [ForceUnroll]
        for (int s = 0; s < IN_SLICES; ++s)
        {
            int idx = NN::get_activation_input(s);
            up[s] = NN::nn_bilinear_apply(
                NN::load_activation(idx, taps.q00), NN::load_activation(idx, taps.q10),
                NN::load_activation(idx, taps.q01), NN::load_activation(idx, taps.q11), taps.t);
        }
    }
    else
    {
        [ForceUnroll]
        for (int s = 0; s < IN_SLICES; ++s) up[s] = float4(0, 0, 0, 0);
    }

    // stage X column
    [ForceUnroll]
    for (int s = 0; s < IN_SLICES; ++s)
    {
        int ic = s * 4;
        Xsh[(ic + 0) * NN_CM_TILE_PIXELS + tile_p] = NN::CoopMat::InputType(up[s].x);
        Xsh[(ic + 1) * NN_CM_TILE_PIXELS + tile_p] = NN::CoopMat::InputType(up[s].y);
        Xsh[(ic + 2) * NN_CM_TILE_PIXELS + tile_p] = NN::CoopMat::InputType(up[s].z);
        Xsh[(ic + 3) * NN_CM_TILE_PIXELS + tile_p] = NN::CoopMat::InputType(up[s].w);
    }

    // stage W
    const int K4     = K / 4;
    const int total4 = OUT_CH_NUM * K4;
    const int pw_idx = denoiser.get_nn_pointwise_index();
    for (uint e = gidx; e < uint(total4); e += 64u)
    {
        int oc      = int(e) / K4;
        int ic_div4 = int(e) % K4;
        float4 w    = NN::load_pointwise(pw_idx, ic_div4, oc);
        int dst     = oc * K + ic_div4 * 4;
        Wsh[dst + 0] = NN::CoopMat::InputType(w.x);
        Wsh[dst + 1] = NN::CoopMat::InputType(w.y);
        Wsh[dst + 2] = NN::CoopMat::InputType(w.z);
        Wsh[dst + 3] = NN::CoopMat::InputType(w.w);
    }

    return tile;
}

public void separable_conv_coopmat<
    bool ADD_RESIDUAL, 
    int OUT_CH_NUM, 
    int IN_SLICES,
    int OUT_SLICES,
    int K_PAD, 
    TAct : NN::ActivationFunctions::IAct>(
        uint3 gid,
        uint3 gtid,
        uint gidx,
        __ref NN::CoopMat::InputType Wsh[],
        __ref NN::CoopMat::InputType Xsh[],
        __ref NN::CoopMat::AccumType Ysh[]
    )
{
    NN::CoopMat::Tile tile = NN::CoopMat::block_pre<IN_SLICES>(
        OUT_CH_NUM, K_PAD, gid, gtid, gidx, Wsh, Xsh);
        
    GroupMemoryBarrierWithGroupSync();

    NN::CoopMat::gemm<OUT_CH_NUM, K_PAD>(gidx, Wsh, Xsh, Ysh);
    
    GroupMemoryBarrierWithGroupSync();

    NN::CoopMat::block_post<OUT_SLICES, OUT_CH_NUM, ADD_RESIDUAL, TAct>(
        tile, gtid, Ysh);
}

// Upsample variant wrapper (for up.slang): bilinear 2x upsample pre-phase, linear
// projection (pass NN::ActivationFunctions::None as TAct), never residual.
public void separable_conv_coopmat_upsample<
    int OUT_CH_NUM,
    int IN_SLICES,
    int OUT_SLICES,
    int K_PAD,
    TAct : NN::ActivationFunctions::IAct>(
        uint3 gid,
        uint3 gtid,
        uint gidx,
        __ref NN::CoopMat::InputType Wsh[],
        __ref NN::CoopMat::InputType Xsh[],
        __ref NN::CoopMat::AccumType Ysh[]
    )
{
    NN::CoopMat::Tile tile = NN::CoopMat::block_pre_upsample<IN_SLICES>(
        OUT_CH_NUM, K_PAD, gid, gtid, gidx, Wsh, Xsh);

    GroupMemoryBarrierWithGroupSync();

    NN::CoopMat::gemm<OUT_CH_NUM, K_PAD>(gidx, Wsh, Xsh, Ysh);

    GroupMemoryBarrierWithGroupSync();

    NN::CoopMat::block_post<OUT_SLICES, OUT_CH_NUM, false, TAct>(
        tile, gtid, Ysh);
}

}
