import resources.hdr_color_output;
import resources.gbuffer;
import utils.tonemapping;

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
    // hdr_color = uread_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI>(input.uv).rgb;
    hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI_FILTERED>(input.uv).rgb;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI_REPROJECTED>(input.uv).rgb;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::BASE>(input.uv).rgb;

    hdr_color = exposure(hdr_color);
    float3 mapped = ACES(hdr_color);
    mapped = gamma(mapped);

    PSOutput output;
    output.color = float4(mapped, 1.0f);
    return output;
}
