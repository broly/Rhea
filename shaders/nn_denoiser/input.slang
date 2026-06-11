// input.slang - transforms AOV from gbuffer + noisy_input to packed activation.
// Layout of channeles in input_packed (strong order by PyTorch's build_input_tensor):
//   ch[0..2]  noisy → demodulated -> log-compressed irradiance
//   ch[3..5]  normal encoded [0,1]
//   ch[6]     depth (log-normalized)
//   ch[7..10] albedo + roughness (all 4 channels)
//   ch[11..12] mvec (if HAS_MVEC)
//   ch[13..15] prev_denoised (if HAS_PREV)
//
// Permutation defines:
//   TOTAL_CHANNELS — general channels num packed (CPU by io_spec)
//   ALBEDO_EPS     — epsilon for demodulation, integer * 1e-6 (e.g. 1000 for 1e-3)
//   CLAMP_MAX      — firefly clamp as integer (for example, 64)
//   HAS_MVEC       — 0|1 (for temporal)
//   HAS_PREV       — 0|1 (for temporal)
#define HAS_MVEC 0
#define HAS_PREV 0

#ifndef TOTAL_CHANNELS
    #error "TOTAL_CHANNELS is missing"
    #define TOTAL_CHANNELS 0
#endif
import nn_denoiser_resource;
import nn_denoiser_api;

import resources.gbuffer;
import resources.hdr_color_output;
import nn_lib;
import channel_writer;
import resources.hdr_color_defines;

#define ALBEDO_EPS 1000     // 1e-3 * 1e6
#define CLAMP_MAX 64

static const float ALBEDO_EPS_F = float(ALBEDO_EPS) * 1e-6;
static const float CLAMP_MAX_F  = float(CLAMP_MAX);
static const int OUT_SLICES = (TOTAL_CHANNELS + 3) / 4;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);
    int2 out_size = denoiser.get_nn_out_size();
    if (p.x >= out_size.x || p.y >= out_size.y) 
        return;

    float2 uv = (float2(p) + 0.5) / float2(denoiser.get_nn_out_size());
    
    float3 noisy = ColorOutput::read<ColorOutput::Slot::RTXGI, 0>(uv).rgb;
    float4 ar = GBuffer::get<GBuffer::Slot::ALBEDO_ROUGHNESS, 0>(uv);
    float3 albedo = ar.rgb;
    float3 normal = GBuffer::get<GBuffer::Slot::NORMAL, 0>(uv).xyz * 2.0 - 1.0;
    float depth = GBuffer::get<GBuffer::Slot::LINEAR_DEPTH, 0>(uv).r;
   
    ChannelWriter<TOTAL_CHANNELS> writer;


    writer << nn_log_compress(nn_demodulate(noisy, albedo, ALBEDO_EPS_F, CLAMP_MAX_F));
    writer << nn_encode_normal(normal);
    writer << nn_normalize_depth(depth, 0.1, 1000.0);
    
    // 7..10: albedo (3) + roughness (1)
    writer << ar;
    
    
#if HAS_MVEC
    float2 mvec    = GBuffer::get<GBuffer::Slot::MOTION_VECTORS>(uv).rg;
#endif
#if HAS_PREV
    // NOW PLACEHOLDER
    float3 prev    = float3(0.0, 0.0, 0.0);
#endif

#if HAS_MVEC
    writer.push(mvec);
#endif
#if HAS_PREV
#error "not supported yet"
    // int prev_start = 11 + (HAS_MVEC ? 2 : 0);
    // ch[prev_start + 0] = prev.r;
    // ch[prev_start + 1] = prev.g;
    // ch[prev_start + 2] = prev.b;
#endif
    denoiser.present_writer(p, writer);
}
