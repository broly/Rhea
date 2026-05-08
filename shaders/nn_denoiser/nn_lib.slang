#pragma once

#include "nn_denoiser.slang"

// -----------------------------------------------------------------------------
// Activations
// -----------------------------------------------------------------------------

float nn_gelu(float x)
{
    const float c = 0.7978845608f;   // sqrt(2/pi)
    const float k = 0.044715f;
    return 0.5f * x * (1.0f + tanh(c * (x + k * x * x * x)));
}

float nn_relu(float x) { return max(x, 0.0f); }


// -----------------------------------------------------------------------------
// Coordinate utilities
// -----------------------------------------------------------------------------

int2 nn_clamp_coord(int2 p, int2 sz) { return clamp(p, int2(0), sz - int2(1)); }


// -----------------------------------------------------------------------------
// Weight access (by index in array)
// -----------------------------------------------------------------------------

// Pointwise: 4 weights for in_ch=[4*ic_div4..+3] -> oc.
float4 nn_load_pw(int pw_idx, int ic_div4, int oc)
{
    return u_nn_pw_weights[pw_idx].Load(int3(ic_div4, oc, 0));
}

// Depthwise: 4 weights for chans [4*ic_div4..+3] in pos (kx, ky).
float4 nn_load_dw(int dw_idx, int kx, int ky, int ic_div4)
{
    return u_nn_dw_weights[dw_idx].Load(int4(kx + 1, ky + 1, ic_div4, 0));
}

float nn_load_bias(int bias_idx, int oc)
{
    float4 b4 = u_nn_biases[bias_idx].Load(int3(oc >> 2, 0, 0));
    return b4[oc & 3];
}


// -----------------------------------------------------------------------------
// Activation slice access
// -----------------------------------------------------------------------------

float4 nn_load_act(int act_idx, int2 p)
{
    return u_nn_activations[act_idx][p];
}

void nn_store_act(int act_idx, int2 p, float4 v)
{
    u_nn_activations[act_idx][p] = v;
    
}


// -----------------------------------------------------------------------------
// Preprocessing helpers (input pass)
// -----------------------------------------------------------------------------

float3 nn_demodulate(float3 noisy, float3 albedo, float eps, float clamp_max)
{
    float3 irr = noisy / max(albedo, float3(eps, eps, eps));
    return clamp(irr, float3(0.0f, 0.0f, 0.0f), float3(clamp_max, clamp_max, clamp_max));
}

float3 nn_log_compress(float3 x) { return log(1.0f + max(x, float3(0.0f, 0.0f, 0.0f))); }
float3 nn_log_expand(float3 x) { return max(exp(x) - 1.0f, float3(0.0f, 0.0f, 0.0f)); }
float3 nn_remodulate(float3 irr, float3 albedo) { return irr * albedo; }
float3 nn_encode_normal(float3 n) { return n * 0.5f + 0.5f; }

float nn_normalize_depth(float depth, float near_v, float far_v)
{
    float d = clamp(depth, near_v, far_v);
    return (log(d) - log(near_v)) / (log(far_v) - log(near_v));
}

int nn_get_act_in (int s) { return g_nn_pass_indices.uActIn [s >> 2][s & 3]; }
int nn_get_act_out(int s) { return g_nn_pass_indices.uActOut[s >> 2][s & 3]; }
int nn_get_act_res(int s) { return g_nn_pass_indices.uActRes[s >> 2][s & 3]; }


// -----------------------------------------------------------------------------
// Bilinear sampling helper (Up pass)
// -----------------------------------------------------------------------------

struct NNBilinearTaps
{
    int2 q00, q10, q01, q11;
    float2 t;
};

NNBilinearTaps nn_bilinear_taps(int2 dst_p, int2 src_size)
{
    float2 srcF = float2(dst_p) * 0.5f - 0.25f;
    int2   src0 = int2(floor(srcF));
    NNBilinearTaps r;
    r.q00 = nn_clamp_coord(src0,                   src_size);
    r.q10 = nn_clamp_coord(src0 + int2(1, 0),       src_size);
    r.q01 = nn_clamp_coord(src0 + int2(0, 1),       src_size);
    r.q11 = nn_clamp_coord(src0 + int2(1, 1),       src_size);
    r.t   = srcF - float2(src0);
    return r;
}

float4 nn_bilinear_apply(float4 v00, float4 v10, float4 v01, float4 v11, float2 t)
{
    float4 r0 = lerp(v00, v10, t.x);
    float4 r1 = lerp(v01, v11, t.x);
    return lerp(r0, r1, t.y);
}


// =============================================================================
// GENERIC COMPUTE PRIMITIVES
// =============================================================================

// -----------------------------------------------------------------------------
// Depthwise 3x3 for tensor, packed to IN_SLICES vec4-slices.
//
// Reads u_nn_activations[uActIn[0..IN_SLICES-1]] around p (3x3 neighborhood),
// Applies depthwise weights from tex by index dw_idx.
// -----------------------------------------------------------------------------
void nn_depthwise_3x3<int IN_SLICES>(
    int2 p,
    int2 out_size,
    int  dw_idx,
    out float4 dwAccum[IN_SLICES])
{
    [ForceUnroll] 
    for (int s = 0; s < IN_SLICES; ++s)
        dwAccum[s] = float4(0, 0, 0, 0);

    [ForceUnroll] 
    for (int ky = -1; ky <= 1; ++ky)
    {
        [ForceUnroll] 
        for (int kx = -1; kx <= 1; ++kx)
        {
            int2 q = nn_clamp_coord(p + int2(kx, ky), out_size);
            [ForceUnroll] 
            for (int s = 0; s < IN_SLICES; ++s)
            {
                float4 a = nn_load_act(nn_get_act_in(s), q);
                float4 w = nn_load_dw(dw_idx, kx, ky, s);
                dwAccum[s] += a * w;
            }
        }
    }
}


// -----------------------------------------------------------------------------
// Pointwise (1x1 conv) + bias + activation.
//
// Accepts depthwise (or upsampled vec4-slices) applies weight matrix.
// -----------------------------------------------------------------------------

// generic fucntor
#ifndef NN_ACT_NONE
    #define NN_ACT_NONE 0
#endif
#ifndef NN_ACT_GELU
    #define NN_ACT_GELU 1
#endif
#ifndef NN_ACT_RELU
    #define NN_ACT_RELU 2
#endif

void nn_pointwise_bias_act<int IN_SLICES, int OUT_CH_NUM, int ACT_KIND>(
    in    float4 dwAccum[IN_SLICES],
    int   pw_idx,
    int   bias_idx,
    out   float result[OUT_CH_NUM])
{
    [ForceUnroll] for (int oc = 0; oc < OUT_CH_NUM; ++oc)
    {
        float acc = nn_load_bias(bias_idx, oc);
        [ForceUnroll] for (int s = 0; s < IN_SLICES; ++s)
        {
            float4 w = nn_load_pw(pw_idx, s, oc);
            acc += dot(w, dwAccum[s]);
        }
        if (ACT_KIND == NN_ACT_GELU) acc = nn_gelu(acc);
        else if (ACT_KIND == NN_ACT_RELU) acc = nn_relu(acc);
        result[oc] = acc;
    }
}


// -----------------------------------------------------------------------------
// Add residual: result += [readback from uActRes slices].
//
// OUT_SLICES = ceil(OUT_CH_NUM / 4). Supposed OUT_CH_NUM multiples by 4 for
// bottleneck residual blocks (see unet.py).
// -----------------------------------------------------------------------------
void nn_add_residual<int OUT_SLICES, int OUT_CH_NUM>(
    int2 p,
    inout float result[OUT_CH_NUM])
{
    [ForceUnroll] for (int s = 0; s < OUT_SLICES; ++s)
    {
        float4 rv = nn_load_act(nn_get_act_res(s), p);
        if (s * 4 + 0 < OUT_CH_NUM) result[s * 4 + 0] += rv.x;
        if (s * 4 + 1 < OUT_CH_NUM) result[s * 4 + 1] += rv.y;
        if (s * 4 + 2 < OUT_CH_NUM) result[s * 4 + 2] += rv.z;
        if (s * 4 + 3 < OUT_CH_NUM) result[s * 4 + 3] += rv.w;
    }
}


// -----------------------------------------------------------------------------
// Pack flat result[OUT_CH_NUM] to RGBA-slices and writes to u_nn_activations.
// -----------------------------------------------------------------------------
void nn_store_packed<int OUT_SLICES, int OUT_CH_NUM>(
    int2 p,
    in float result[OUT_CH_NUM])
{
    [ForceUnroll] 
    for (int s = 0; s < OUT_SLICES; ++s)
    {
        float4 v = float4(0, 0, 0, 0);
        if (s * 4 + 0 < OUT_CH_NUM) 
            v.x = result[s * 4 + 0];
        if (s * 4 + 1 < OUT_CH_NUM) 
            v.y = result[s * 4 + 1];
        if (s * 4 + 2 < OUT_CH_NUM) 
            v.z = result[s * 4 + 2];
        if (s * 4 + 3 < OUT_CH_NUM) 
            v.w = result[s * 4 + 3];
        nn_store_act(nn_get_act_out(s), p, v);
    }
    
}


// -----------------------------------------------------------------------------
// 2x2 average pool (for Down pass).
// -----------------------------------------------------------------------------
float4 nn_avg_pool_2x2(int act_idx, int2 dst_p)
{
    float4 a = nn_load_act(act_idx, int2(dst_p.x*2,     dst_p.y*2  ));
    float4 b = nn_load_act(act_idx, int2(dst_p.x*2 + 1, dst_p.y*2  ));
    float4 c = nn_load_act(act_idx, int2(dst_p.x*2,     dst_p.y*2 + 1));
    float4 d = nn_load_act(act_idx, int2(dst_p.x*2 + 1, dst_p.y*2 + 1));
    return 0.25f * (a + b + c + d);
}
