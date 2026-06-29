import resources.hdr_color_output;
import resources.gbuffer;
import resources.hdr_color_defines;
import utils.tonemapping;
import nn_denoiser.nn_denoiser_resource;
import utils.draw_number;

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

struct PushConstants
{
    float time;
    uint32_t output_mode;
    uint32_t fps;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

[shader("fragment")]
PSOutput main(VSOutput input)
{
    float3 hdr_color;
    
    float time_clamped = max(pc.time - 20, 0.0) * 2;
    int index = 0;
    
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI>(input.uv).rgb;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI_FILTERED>(input.uv).rgb;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::RTXGI_REPROJECTED>(input.uv).rgb;
    // hdr_color = read_hdr_color_present<COLOR_OUTPUT_HDR::BASE>(input.uv).rgb;
    // hdr_color = get_gbuffer<GBUFFER_SLOT::GBUFFER_SLOT_NORMAL>(input.uv).rgb;
    // uint width, height;
    // u_nn_activations[0].GetDimensions(width, height);
    // int2 coord = int2(input.uv * float2(32, 32));
    
    // hdr_color =  float3(width, height, 0.0); // u_nn_activations[0][coord].rgb;
    //switch (pc.output_mode)
    //{
    //    case 0:
    //            hdr_color = ColorOutput::read<ColorOutput::Slot::BASE>(input.uv).rgb;
    //        break;
    //    case 1:
    //        hdr_color = ColorOutput::read<ColorOutput::Slot::RTXGI_NEURAL_DENOISED>(input.uv).rgb;
    //        break;
    //    case 2:
    //        hdr_color = ColorOutput::read<ColorOutput::Slot::RTXGI>(input.uv).rgb;
    //        break;
    //}
    
    hdr_color = ColorOutput::read<ColorOutput::Slot::RTXGI_NEURAL_DENOISED>(input.uv).rgb;
    // hdr_color = ColorOutput::read<ColorOutput::Slot::RTXGI>(input.uv).rgb;
    
    // {
    //      index = min((int)time_clamped, NN_NUM_DW_TEXTURES);
    //      int depth_index = 0;// (int)time_clamped;
    //      int array_layer_index = index;
    //      
    //      uint width, height, depth;
    //      u_nn_dw_weights[array_layer_index].GetDimensions(width, height, depth);
    //      int4 coord = int4((int)(input.uv.x*width), (int)(input.uv.y*height), depth_index, 0);
    //      hdr_color = u_nn_dw_weights[array_layer_index].Load(coord).rgb;
    //      hdr_color = DrawNumber99(input.uv, hdr_color, depth, float2(0.12, 0.42), 0.08, float3(1.0, 0.7, 0.1));
    // }
    // {
    //      index = min((int)time_clamped, NN_NUM_PW_TEXTURES - 1);
    //      uint width, height;
    //      u_nn_pw_weights[index].GetDimensions(width, height);
    //      int3 coord = int3((int)(input.uv.x*width), (int)(input.uv.y*height), 0);
    //      hdr_color = u_nn_pw_weights[index].Load(coord).rgb;
    // }
    // {
    //      index = min((int)time_clamped, NN_NUM_BIAS_TEXTURES);
    //      uint width, height;
    //      u_nn_biases[index].GetDimensions(width, height);
    //      int3 coord = int3((int)(input.uv.x*width), (int)(input.uv.y*height), 0);
    //      hdr_color = u_nn_biases[index].Load(coord).rgb;
    // }
    
    
    // {
    //     index = min((int)(time_clamped), NN_NUM_ACTIVATION_SLOTS - 1);
    //     uint w, h;
    //     u_nn_activations[index].GetDimensions(w, h);
    //     
    //     int2 texel = int2(input.uv * float2(w, h));
    //     
    //     
    //     hdr_color = u_nn_activations[index][texel].rgb;
    //     
    // }
    //{
    //    uint w, h;
    //    u_nn_activations[515].GetDimensions(w, h);
    //    int2 texel = int2(input.uv * float2(w, h));
    //    hdr_color = u_nn_activations[514][texel].rgb;
  //
    //}
    
    
    hdr_color = exposure(hdr_color);
    
    hdr_color = DrawNumber9999(input.uv, hdr_color, pc.fps, float2(0.12), 0.08, float3(1.0, 0.2, 0.1));
    
    float3 mapped = ACES(hdr_color);
    mapped = gamma(mapped);

    PSOutput output;
    output.color = float4(mapped, 1.0f);
    return output;
}
