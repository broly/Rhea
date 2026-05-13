import nn_denoiser_resource;
import nn_lib;
import resources.gbuffer;
import resources.hdr_color_output;
import resources.hdr_color_storage;
import resources.hdr_color_defines;

#ifndef IN_CH
    #error "IN_CH define missing"
    #define IN_CH 0
#endif

#ifndef ALBEDO_EPS
    #define ALBEDO_EPS 1000     // 1e-3 * 1e6  (same as input.slang)
#endif
#ifndef CLAMP_MAX
    #define CLAMP_MAX 64
#endif

static const float ALBEDO_EPS_F = float(ALBEDO_EPS) * 1e-6;
static const float CLAMP_MAX_F  = float(CLAMP_MAX);

static const int IN_SLICES = (IN_CH + 3) / 4;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    int2 p = int2(dtid.xy);

    int2 out_size = denoiser.get_nn_out_size();

    if (p.x >= out_size.x || p.y >= out_size.y)
        return;

    float2 uv = (float2(p) + 0.5) / float2(out_size);

    // -----------------------------------------------------------------
    // 1x1 conv: feature slices -> RGB residual.
    // -----------------------------------------------------------------
    float4 feat[IN_SLICES];

    [ForceUnroll]
    for (int s = 0; s < IN_SLICES; ++s)
    {
        feat[s] = nn_load_act(nn_get_act_in(s), p);
    }

    float3 residual;
    [ForceUnroll]
    for (int oc = 0; oc < 3; ++oc)
    {
        float acc = nn_load_bias(denoiser.get_nn_bias_index(), oc);

        [ForceUnroll]
        for (int s = 0; s < IN_SLICES; ++s)
        {
            float4 w = nn_load_pw(denoiser.get_nn_pointwise_index(), s, oc);
            acc += dot(w, feat[s]);
        }
        if (oc == 0)
            residual.r = acc;
        else if (oc == 1)
            residual.g = acc;
        else
            residual.b = acc;
    }

    float3 noisy  = ColorOutput::read<ColorOutput::Slot::RTXGI, 0>(uv).rgb;
    float3 albedo = nn_load_packed_rgb(get_nn_albedo_flat_ch(), p);

    float3 irr_log_baseline = nn_log_compress(nn_demodulate(noisy, albedo, ALBEDO_EPS_F, CLAMP_MAX_F));

    float3 logIrr   = irr_log_baseline + residual;
    float3 irr      = nn_log_expand(logIrr);
    float3 finalRGB = nn_remodulate(irr, albedo);

    ColorOutput::write<ColorOutput::Slot::RTXGI_NEURAL_DENOISED>(p, finalRGB);
}
