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

#include "../resources/gbuffer.slang"
#include "../resources/hdr_color_output.slang"
#include "nn_denoiser.slang"
#include "nn_lib.slang"

#ifndef TOTAL_CHANNELS
    #error "TOTAL_CHANNELS define missing"
    #define TOTAL_CHANNELS 0
#endif
#ifndef ALBEDO_EPS
    #define ALBEDO_EPS 1000     // 1e-3 * 1e6
#endif
#ifndef CLAMP_MAX
    #define CLAMP_MAX 64
#endif
#ifndef HAS_MVEC
    #define HAS_MVEC 0
#endif
#ifndef HAS_PREV
    #define HAS_PREV 0
#endif

static const float ALBEDO_EPS_F = float(ALBEDO_EPS) * 1e-6;
static const float CLAMP_MAX_F  = float(CLAMP_MAX);

static const int OUT_SLICES = (TOTAL_CHANNELS + 3) / 4;

int nn_reflect_101(int x, int size)
{
    if (x < 0)
        return -x;

    if (x >= size)
        return 2 * size - x - 2;

    return x;
}

int2 nn_reflect_coord(int2 p, int2 size)
{
    return int2(
        nn_reflect_101(p.x, size.x),
        nn_reflect_101(p.y, size.y)
    );
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);
    int2 out_size = denoiser.get_nn_out_size();
    if (p.x >= out_size.x || p.y >= out_size.y) return;

    float2 uv = (float2(p) + 0.5) / float2(denoiser.get_nn_out_size());
    // int2 rp = nn_reflect_coord(p, out_size);
    // float2 uv = (float2(rp) + 0.5) / float2(out_size);

    // === Read AOVs ===
    float3 noisy   = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI, 0>(uv).rgb;
    float3 normal  = get_gbuffer<GBUFFER_SLOT::GBUFFER_SLOT_WORLD_NORMAL, 0>(uv).xyz * 2.0 - 1.0;
    float  depth   = get_gbuffer<GBUFFER_SLOT::GBUFFER_SLOT_LINEAR_DEPTH, 0>(uv).r;
    float4 ar      = get_gbuffer<GBUFFER_SLOT::GBUFFER_SLOT_ALBEDO_ROUGHNESS, 0>(uv);
    float3 albedo  = ar.rgb;

#if HAS_MVEC
    float2 mvec    = get_gbuffer<GBUFFER_SLOT::GBUFFER_SLOT_MOTION_VECTORS>(uv).rg;
#endif
#if HAS_PREV
    // NOW PLACEHOLDER
    float3 prev    = float3(0.0, 0.0, 0.0);
#endif

    // === Build channels ===
    float ch[TOTAL_CHANNELS];

    // 0..2: irradiance (log-compressed demodulated noisy)
    {
        float3 irr_log = nn_log_compress(
            nn_demodulate(noisy, albedo, ALBEDO_EPS_F, CLAMP_MAX_F));
        ch[0] = irr_log.r;
        ch[1] = irr_log.g;
        ch[2] = irr_log.b;
    }

    // 3..5: normal encoded
    {
        float3 n_enc = nn_encode_normal(normal);
        ch[3] = n_enc.r;
        ch[4] = n_enc.g;
        ch[5] = n_enc.b;
    }

    // 6: depth normalized
    ch[6] = nn_normalize_depth(depth, 0.1, 1000.0);

    // 7..10: albedo (3) + roughness (1)
    ch[7]  = ar.r;
    ch[8]  = ar.g;
    ch[9]  = ar.b;
    ch[10] = ar.a;

#if HAS_MVEC
    ch[11] = mvec.x;
    ch[12] = mvec.y;
#endif
#if HAS_PREV
    int prev_start = 11 + (HAS_MVEC ? 2 : 0);
    ch[prev_start + 0] = prev.r;
    ch[prev_start + 1] = prev.g;
    ch[prev_start + 2] = prev.b;
#endif

    // === Pack to slices ===
    nn_store_packed<OUT_SLICES, TOTAL_CHANNELS>(p, ch);
}
