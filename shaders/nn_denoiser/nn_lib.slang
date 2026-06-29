#pragma once

import nn_denoiser_resource;

// -----------------------------------------------------------------------------
// Activations
// -----------------------------------------------------------------------------

namespace NN
{

public float gelu(float x)
{
    const float c = 0.7978845608f;   // sqrt(2/pi)
    const float k = 0.044715f;
    return 0.5f * x * (1.0f + tanh(c * (x + k * x * x * x)));
}

public float relu(float x) { return max(x, 0.0f); }


// -----------------------------------------------------------------------------
// Coordinate utilities
// -----------------------------------------------------------------------------

public int2 clamp_coord(int2 p, int2 sz) 
{ 
    return clamp(p, int2(0), sz - int2(1)); 
}




// -----------------------------------------------------------------------------
// Weight access (by index in array)
// -----------------------------------------------------------------------------

// Pointwise: 4 weights for in_ch=[4*ic_div4..+3] -> oc.
// Source texel layout was Load(int3(ic_div4, oc, 0)) on a width=PW_W texture,
// i.e. row-major element = oc * width + ic_div4.
public float4 load_pointwise(int pw_idx, int ic_div4, int oc)
{
    denoiser::NNWeightSlot slot = denoiser.get_pw_slot(pw_idx);
    int elem = slot.base + oc * slot.width + ic_div4;
    return denoiser.u_nn_pw_weights[elem];
}

// Depthwise: 4 weights for chans [4*ic_div4..+3] in pos (kx, ky).
// Source texel layout was Load(int4(kx+1, ky+1, ic_div4, 0)) on a
// width=height=3 array texture, i.e. element =
//   ic_div4 * (width*height) + (ky+1) * width + (kx+1).
public float4 load_depthwise(int dw_idx, int kx, int ky, int ic_div4)
{
    denoiser::NNWeightSlot slot = denoiser.get_dw_slot(dw_idx);
    int x = kx + 1;
    int y = ky + 1;
    int elem = slot.base + ic_div4 * (slot.width * slot.height) + y * slot.width + x;
    return denoiser.u_nn_dw_weights[elem];
}

// Bias: source texel layout was Load(int3(oc>>2, 0, 0)), element = oc>>2.
public float load_bias(int bias_idx, int oc)
{
    denoiser::NNWeightSlot slot = denoiser.get_bias_slot(bias_idx);
    float4 b4 = denoiser.u_nn_biases[slot.base + (oc >> 2)];
    return b4[oc & 3];
}


// -----------------------------------------------------------------------------
// Activation slice access
// -----------------------------------------------------------------------------

public float4 load_activation(int act_idx, int2 p)
{
    return denoiser.u_nn_activations[act_idx][p];
}

public void store_activation(int act_idx, int2 p, float4 v)
{
    denoiser.u_nn_activations[act_idx][p] = v;
}

namespace processing
{

// -----------------------------------------------------------------------------
// Preprocessing helpers (input pass)
// -----------------------------------------------------------------------------

public float3 demodulate(float3 noisy, float3 albedo, float eps, float clamp_max)
{
    float3 irr = noisy / max(albedo, float3(eps, eps, eps));
    return clamp(irr, float3(0.0f, 0.0f, 0.0f), float3(clamp_max, clamp_max, clamp_max));
}

public float3 log_compress(float3 x) { return log(1.0f + max(x, float3(0.0f, 0.0f, 0.0f))); }
public float3 log_expand(float3 x) { return max(exp(x) - 1.0f, float3(0.0f, 0.0f, 0.0f)); }
public float3 remodulate(float3 irr, float3 albedo) { return irr * albedo; }
public float3 encode_normal(float3 n) { return n * 0.5f + 0.5f; }

public float normalize_depth(float depth, float near_v, float far_v)
{
    float d = clamp(depth, near_v, far_v);
    return (log(d) - log(near_v)) / (log(far_v) - log(near_v));
}
}


public int get_activation_input(int s)
{
    int4 v = denoiser.u_nn_pass_indices[denoiser.get_pass_index()].uActIn[s >> 2];
    int  c = s & 3;
    
    return v[c];
}

public int get_activation_output(int s)
{
    int4 v = denoiser.u_nn_pass_indices[denoiser.get_pass_index()].uActOut[s >> 2];
    int  c = s & 3;
    return v[c];
}

public int get_activation_residual(int s)
{
    int4 v = denoiser.u_nn_pass_indices[denoiser.get_pass_index()].uActRes[s >> 2];
    int  c = s & 3;
    return v[c];
}




// -----------------------------------------------------------------------------
// Head-pass helper: load 3 channels (an AOV's RGB) from input_packed,
// allowing the channel triple to straddle slice boundaries.
//
// flat_ch is the flat channel index into input_packed where the AOV's
// R component starts. The 3 channels are then at flat_ch, flat_ch+1,
// flat_ch+2 — each of which decodes to (slot, component) via /4 and %4.
//
// uActRes for the head pass is populated by build_pass_indices() with
// the list of input_packed slot ids (one entry per slice of
// input_packed), so uActRes[(flat_ch+k)/4] gives the correct slot for
// the k-th channel of the triple.
// -----------------------------------------------------------------------------
public float3 load_packed_rgb(int flat_ch, int2 p)
{
    float3 result;
    [ForceUnroll]
    for (int k = 0; k < 3; ++k)
    {
        int ch        = flat_ch + k;
        int slice_idx = ch >> 2;
        int comp_idx  = ch & 3;
        int slot      = get_activation_residual(slice_idx);
        float4 v      = load_activation(slot, p);
        float scalar  = v[comp_idx];
        
        result[k] = scalar;
    }
    return result;
}

public int get_baseline_flat_ch()
{
    return denoiser.u_nn_pass_indices[denoiser.get_pass_index()].uBaselineFlatCh;
}

public int get_albedo_flat_ch()
{
    return denoiser.u_nn_pass_indices[denoiser.get_pass_index()].uAlbedoFlatCh;
}


// -----------------------------------------------------------------------------
// Bilinear sampling helper (Up pass)
// -----------------------------------------------------------------------------

public struct NNBilinearTaps
{
    public int2 q00, q10, q01, q11;
    public float2 t;
};

public NNBilinearTaps nn_bilinear_taps(int2 dst_p, int2 src_size)
{
    float2 srcF = float2(dst_p) * 0.5f - 0.25f;
    int2   src0 = int2(floor(srcF));
    NNBilinearTaps r;
    r.q00 = NN::clamp_coord(src0,                   src_size);
    r.q10 = NN::clamp_coord(src0 + int2(1, 0),       src_size);
    r.q01 = NN::clamp_coord(src0 + int2(0, 1),       src_size);
    r.q11 = NN::clamp_coord(src0 + int2(1, 1),       src_size);
    r.t   = srcF - float2(src0);
    return r;
}

public float4 nn_bilinear_apply(float4 v00, float4 v10, float4 v01, float4 v11, float2 t)
{
    float4 r0 = lerp(v00, v10, t.x);
    float4 r1 = lerp(v01, v11, t.x);
    return lerp(r0, r1, t.y);
}


// =============================================================================
// GENERIC COMPUTE PRIMITIVES
// =============================================================================

public bool nn_inside(int2 p, int2 sz)
{
    return p.x >= 0 && p.y >= 0 &&
           p.x < sz.x && p.y < sz.y;
}

// -----------------------------------------------------------------------------
// Depthwise 3x3 for tensor, packed to IN_SLICES vec4-slices.
//
// Reads u_nn_activations[uActIn[0..IN_SLICES-1]] around p (3x3 neighborhood),
// Applies depthwise weights from tex by index dw_idx.
// -----------------------------------------------------------------------------
public void depthwise_3x3<int IN_SLICES>(
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
            int2 q = NN::clamp_coord(p + int2(kx, ky), out_size);
            [ForceUnroll] 
            for (int s = 0; s < IN_SLICES; ++s)
            {
                float4 a = NN::load_activation(get_activation_input(s), q);
                // float4 a = float4(0,0,0,0);
                
                // if (nn_inside(q, out_size))
                // {
                //     a = nn_load_act(nn_get_act_in(s), q);
                // }   
                float4 w = load_depthwise(dw_idx, kx, ky, s);
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


namespace ActivationFunctions
{
    public interface IAct
    {
        public static float apply(float x);
    }

    public struct None : IAct
    {
        public static float apply(float x) { return x; }
    };
    
    public struct ReLU : IAct
    {
        public static float apply(float x) { return relu(x); }
    }
    
    public struct GeLU : IAct
    {
        public static float apply(float x) { return gelu(x); }
    }
}

public void pointwise_bias_activate<int IN_SLICES, int OUT_CH_NUM, TAct : ActivationFunctions::IAct>(
    in float4 dwAccum[IN_SLICES],
    int pw_idx,
    int bias_idx,
    out float result[OUT_CH_NUM])
{
    // maybe [DoNotUnroll]
    for (int oc = 0; oc < OUT_CH_NUM; ++oc)
    {
        float acc = NN::load_bias(bias_idx, oc);

        [ForceUnroll]
        for (int s = 0; s < IN_SLICES; ++s)
        {
            float4 w = load_pointwise(pw_idx, s, oc);
            acc += dot(w, dwAccum[s]);
        }

        acc = TAct::apply(acc);

        result[oc] = acc;
    }
}

internal void store_slice<int NUM>(inout float array[NUM], int s, float4 value)
{
    [ForceUnroll]
    for (int ch_idx = 0; ch_idx < 4; ch_idx++)
    {
        if (s * 4 + ch_idx < NUM)
            array[s * 4 + ch_idx] += value[ch_idx];
    }
}

// -----------------------------------------------------------------------------
// Add residual: result += [readback from uActRes slices].
//
// OUT_SLICES = ceil(OUT_CH_NUM / 4). Supposed OUT_CH_NUM multiples by 4 for
// bottleneck residual blocks (see unet.py).
// -----------------------------------------------------------------------------
public void add_residual<int OUT_SLICES, int OUT_CH_NUM>(
    int2 p,
    inout float result[OUT_CH_NUM])
{
    [ForceUnroll] 
    for (int s = 0; s < OUT_SLICES; ++s)
    {
        float4 rv = load_activation(get_activation_residual(s), p);
        store_slice(result, s, rv);
    }
}

internal float4 get_slice<int ARRAY_SIZE>(in float array[ARRAY_SIZE], int slice)
{
    float4 v = float4(0, 0, 0, 0);
    
    [ForceUnroll]
    for (int index = 0; index < 4; index++)
    {
        if (slice * 4 + index < ARRAY_SIZE)
        {
            v[index] = array[slice * 4 + index];
        }
    }
    return v;
}

// -----------------------------------------------------------------------------
// Pack flat result[OUT_CH_NUM] to RGBA-slices and writes to u_nn_activations.
// -----------------------------------------------------------------------------
public void store_packed<int OUT_SLICES, int OUT_CH_NUM>(
    int2 p,
    in float result[OUT_CH_NUM])
{
    [ForceUnroll] 
    for (int s = 0; s < OUT_SLICES; ++s)
    {
        float4 v = get_slice(result, s);
        int act_index = get_activation_output(s);
        store_activation(act_index, p, v);
    }
    
}


// -----------------------------------------------------------------------------
// 2x2 average pool (for Down pass).
// -----------------------------------------------------------------------------
public float4 avg_pool_2x2(int act_idx, int2 dst_p)
{
    float4 a = load_activation(act_idx, int2(dst_p.x*2,     dst_p.y*2  ));
    float4 b = load_activation(act_idx, int2(dst_p.x*2 + 1, dst_p.y*2  ));
    float4 c = load_activation(act_idx, int2(dst_p.x*2,     dst_p.y*2 + 1));
    float4 d = load_activation(act_idx, int2(dst_p.x*2 + 1, dst_p.y*2 + 1));
    return 0.25f * (a + b + c + d);
}

}


