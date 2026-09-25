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
    uint32_t view_mode;
};

// view_mode, keep in sync with DebugViewMode (src/game/debug_view.ixx)
static const uint VIEW_LIT             = 0;
static const uint VIEW_WIREFRAME       = 1;
static const uint VIEW_LIT_WIREFRAME   = 2;
static const uint VIEW_BASE_COLOR      = 3;
static const uint VIEW_ROUGHNESS       = 4;
static const uint VIEW_WORLD_NORMAL    = 5;
static const uint VIEW_VIEW_NORMAL     = 6;
static const uint VIEW_GEOMETRY_NORMAL = 7;
static const uint VIEW_LINEAR_DEPTH    = 8;
static const uint VIEW_WORLD_POSITION  = 9;
static const uint VIEW_MOTION_VECTORS  = 10;
static const uint VIEW_EMISSIVE        = 11;

static const float3 DEBUG_VIEW_BACKGROUND = float3(0.03, 0.03, 0.035);

// Debug views output display values directly (no exposure / tonemapping).
// Returns false for the views that go through the regular tonemapping.
bool get_debug_view_color(uint view_mode, float2 uv, out float3 color)
{
    color = float3(0.0);
    
    if (view_mode == VIEW_LIT || view_mode == VIEW_LIT_WIREFRAME)
        return false;
    
    // linear depth is cleared to 0, any surface written by the base pass is in front of the near plane
    const float linear_depth = GBuffer::get<GBuffer::Slot::LINEAR_DEPTH>(uv).r;
    if (linear_depth <= 0.0)
    {
        color = DEBUG_VIEW_BACKGROUND;
        return true;
    }
    
    const float4 world_normal = GBuffer::get<GBuffer::Slot::WORLD_NORMAL>(uv);
    
    switch (view_mode)
    {
        case VIEW_WIREFRAME:
        {
            // dim clay shading: keeps shapes readable under the wires
            const float3 N = normalize(world_normal.xyz * 2.0 - 1.0);
            const float ndl = dot(N, normalize(float3(0.35, 0.85, 0.4))) * 0.5 + 0.5;
            color = float3(0.05, 0.055, 0.065) + ndl * float3(0.09, 0.09, 0.1);
            break;
        }
        case VIEW_BASE_COLOR:
            // stored linear (pow 2.2 in geometry.frag)
            color = pow(GBuffer::get<GBuffer::Slot::ALBEDO_ROUGHNESS>(uv).rgb, float3(1.0 / 2.2));
            break;
        case VIEW_ROUGHNESS:
            color = GBuffer::get<GBuffer::Slot::ALBEDO_ROUGHNESS>(uv).aaa;
            break;
        case VIEW_WORLD_NORMAL:
            color = world_normal.rgb;
            break;
        case VIEW_VIEW_NORMAL:
            color = GBuffer::get<GBuffer::Slot::NORMAL>(uv).rgb;
            break;
        case VIEW_GEOMETRY_NORMAL:
            color = GBuffer::get<GBuffer::Slot::GEOMETRY_NORMAL>(uv).rgb;
            break;
        case VIEW_LINEAR_DEPTH:
        {
            // log scale: near is white, ~500 m fades to black
            const float t = saturate(log2(1.0 + linear_depth) / log2(1.0 + 500.0));
            color = float3(1.0 - t);
            break;
        }
        case VIEW_WORLD_POSITION:
        {
            // 1 m cells + thin grid lines on the cell borders
            const float3 p = GBuffer::get<GBuffer::Slot::POSITION>(uv).xyz;
            const float3 cell = frac(p);
            const float3 border = min(cell, 1.0 - cell);
            const float grid = step(min(border.x, min(border.y, border.z)), 0.02);
            color = lerp(cell, float3(1.0), grid * 0.6);
            break;
        }
        case VIEW_MOTION_VECTORS:
        {
            // uv delta per frame, gray = static
            const float2 mv = GBuffer::get<GBuffer::Slot::MOTION_VECTORS>(uv).xy;
            color = float3(saturate(0.5 + mv * 25.0), 0.5);
            break;
        }
        case VIEW_EMISSIVE:
            color = GBuffer::get<GBuffer::Slot::EMISSIVE>(uv).rgb;
            break;
        default:
            // unknown mode -> magenta
            color = float3(1.0, 0.0, 1.0);
            break;
    }
    return true;
}

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

[shader("fragment")]
PSOutput main(VSOutput input)
{
    float3 hdr_color;
    
    float time_clamped = max(pc.time - 20, 0.0) * 2;
    int index = 0;
    
    float3 debug_color;
    if (get_debug_view_color(pc.view_mode, input.uv, debug_color))
    {
        PSOutput debug_output;
        debug_output.color = float4(DrawNumber9999(input.uv, debug_color, pc.fps, float2(0.12), 0.08, float3(1.0, 0.2, 0.1)), 1.0f);
        return debug_output;
    }
    
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
    
    // output_mode (keys 0/1/2): final lit image / SVGF-filtered GI / raw RTXGI; 3 = neural denoiser output
    switch (pc.output_mode)
    {
        case 1:
            hdr_color = ColorOutput::read<ColorOutput::Slot::RTXGI_FILTERED>(input.uv).rgb;
            break;
        case 2:
            hdr_color = ColorOutput::read<ColorOutput::Slot::RTXGI>(input.uv).rgb;
            break;
        case 3:
            hdr_color = ColorOutput::read<ColorOutput::Slot::RTXGI_NEURAL_DENOISED>(input.uv).rgb;
            break;
        default:
            hdr_color = ColorOutput::read<ColorOutput::Slot::BASE>(input.uv).rgb;
            break;
    }
    
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
