import resources.hdr_color_output;
import resources.gbuffer;
import utils.tonemapping;
import nn_denoiser.nn_denoiser;

static const float NEAR = 0.5f;
static const float FAR  = 2000.0f;

float3 exposure(float3 v)
{
    return v * 0.6f;
}

float3 gamma(float3 v)
{
    return pow(v, float3(1.0f / 3.7f, 1.0f / 3.7f, 1.0f / 3.7f));
}

struct VSOutput
{
    [[vk::location(0)]] float2 uv : UV;
};

struct PSOutput
{
    [[vk::location(0)]] float4 color : SV_Target0;
};

[shader("fragment")]
PSOutput main(VSOutput input)
{
    float3 hdr_color;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI>(input.uv).rgb;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI_FILTERED>(input.uv).rgb;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI_REPROJECTED>(input.uv).rgb;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::BASE>(input.uv).rgb;
    // hdr_color = get_gbuffer<GBUFFER_SLOT::GBUFFER_SLOT_NORMAL>(input.uv).rgb;
    // uint width, height;
    // u_nn_activations[0].GetDimensions(width, height);
    // int2 coord = int2(input.uv * float2(32, 32));
    
    // hdr_color =  float3(width, height, 0.0); // u_nn_activations[0][coord].rgb;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI_NEURAL_DENOISED>(input.uv).rgb;
    // hdr_color = u_nn_biases[17].Sample(input.uv).rgb;
    
    // hdr_color = u_nn_activations[0][coord].rgb;
    
    uint w, h;
    u_nn_activations[8].GetDimensions(w, h);
    
    int2 texel = int2(input.uv * float2(w, h));
    
    hdr_color = u_nn_activations[8][texel].rgb;

    hdr_color = exposure(hdr_color);
    float3 mapped = ACES(hdr_color);
    mapped = gamma(mapped);

    PSOutput output;
    output.color = float4(mapped, 1.0f);
    return output;
}
