import resources.hdr_color_output;
import resources.gbuffer;
import resources.gtao;
import resources.hdr_color_defines;
import utils.tonemapping;
import nn_denoiser.nn_denoiser_resource;
import utils.draw_number;
import resources.post_process;

// The last pass of the frame (GameRenderGraph "ToneMapping"): the lit HDR frame (after TAA) through the
// post processing of PostProcessRenderer (src/game/post_process.*) into the swapchain:
//   scene color      chromatic aberration, sharpen
//   HDR (linear)     bloom, exposure (auto: auto_exposure.comp), vignette, white balance x color filter,
//                    contrast, hue shift x saturation
//   tone curve       Tonemapper (utils/tonemapping.slang)
//   display space    display gamma, lift / gamma / gain, split toning, posterize, film grain, dither
// The parameters (PostProcessUBO) are built on the CPU from the render.post.* cvars.

static const float MIDDLE_GRAY = 0.18;

float luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// the lit frame, clamped inside the screen (the hdr color sampler repeats)
float3 read_scene(float2 uv)
{
    const float2 half_texel = 0.5 * PostProcess::params.screen.zw;
    return ColorOutput::read<ColorOutput::Slot::BASE>(clamp(uv, half_texel, 1.0 - half_texel)).rgb;
}

float3 scene_color(float2 uv)
{
    const float aberration = PostProcess::params.lens.z;
    float3 color;
    if (aberration > 0.0)
    {
        // lateral chromatic aberration: red and blue images scaled apart from the center, by `aberration`
        // of the screen at its edges
        const float2 offset = (uv - 0.5) * 2.0 * aberration;
        color.r = read_scene(uv - offset).r;
        color.g = read_scene(uv).g;
        color.b = read_scene(uv + offset).b;
    }
    else
    {
        color = read_scene(uv);
    }

    // sharpen against the TAA softness: unsharp mask of the luminance with the 4 neighbors, computed on
    // l / (1 + l) so bright edges do not ring
    const float sharpen = PostProcess::params.lens.w;
    if (sharpen > 0.0)
    {
        const float2 t = PostProcess::params.screen.zw;
        const float l_center = luma(color);
        const float l_neighbors = 0.25 * (luma(read_scene(uv + float2(t.x, 0.0))) + luma(read_scene(uv - float2(t.x, 0.0)))
                                        + luma(read_scene(uv + float2(0.0, t.y))) + luma(read_scene(uv - float2(0.0, t.y))));
        const float c_center = l_center / (1.0 + l_center);
        const float c_neighbors = l_neighbors / (1.0 + l_neighbors);
        const float c_sharp = clamp(c_center + (c_center - c_neighbors) * sharpen * 1.5, 0.0, 0.995);
        const float l_sharp = c_sharp / (1.0 - c_sharp);
        color *= l_center > 1e-6 ? l_sharp / l_center : 1.0;
    }
    return color;
}

float3 soft_light(float3 base, float3 blend)
{
    const float3 r1 = 2.0 * base * blend + base * base * (1.0 - 2.0 * blend);
    const float3 r2 = sqrt(base) * (2.0 * blend - 1.0) + 2.0 * base * (1.0 - blend);
    const float3 t = step(0.5, blend);
    return r2 * t + (1.0 - t) * r1;
}

// hash without sine (Dave Hoskins), 0..1
float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float value_noise(float2 p)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    const float a = hash12(i);
    const float b = hash12(i + float2(1.0, 0.0));
    const float c = hash12(i + float2(0.0, 1.0));
    const float d = hash12(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// exposure of this frame, EV (the scene is multiplied by 2^EV)
float exposure_ev()
{
    const PostProcess::Params p = PostProcess::params;
    float ev = p.exposure.y;
    if (p.exposure.x > 0.5)
    {
        const float4 state = PostProcess::exposure_state();
        if (state.w > 0.5 && !isnan(state.x) && !isinf(state.x))
            ev = state.x;
    }
    return ev + p.exposure.z;
}

// the luminance histogram of the auto exposure, bottom left: bars of the bins (log2 luminance), yellow: the
// metered average, white: the luminance the exposure brings to middle gray, red: the min / max EV limits
float3 draw_histogram(float2 uv, float3 display, float ev)
{
    const PostProcess::Params p = PostProcess::params;
    const float2 origin = float2(0.02, 0.76);
    const float2 extent = float2(0.32, 0.2);
    const float2 r = (uv - origin) / extent;
    if (any(r < 0.0) || any(r > 1.0))
        return display;

    display = lerp(display, float3(0.02, 0.02, 0.025), 0.8);
    const float bar = PostProcess::histogram_bin(int(r.x * PostProcess::HISTOGRAM_BINS));
    if (1.0 - r.y < bar)
        display = float3(0.3, 0.5, 0.85);

    const float log_min = p.histogram.x;
    const float log_range = max(p.histogram.y, 1.0);
    const float line_width = 1.5 * p.screen.z / extent.x;
    const float key = p.auto_exposure.z;
    const float4 state = PostProcess::exposure_state();
    const float markers[4] = { state.z, key - ev, key - p.auto_exposure.x, key - p.auto_exposure.y };
    const float3 colors[4] = { float3(1.0, 0.85, 0.2), float3(1.0, 1.0, 1.0), float3(0.9, 0.2, 0.2), float3(0.9, 0.2, 0.2) };
    for (int i = 0; i < 4; ++i)
    {
        const float x = (markers[i] - log_min) / log_range;
        if (abs(r.x - x) < line_width)
            display = colors[i];
    }
    return display;
}

// the post processing of the lit frame, display values 0..1
float3 post_process(float2 uv, float2 pixel)
{
    const PostProcess::Params p = PostProcess::params;

    float3 color = scene_color(uv);

    // bloom: a share of the frame replaced by its blurred copy (keeps the energy, no threshold)
    if (p.bloom.w > 0.0)
        color = lerp(color, PostProcess::bloom(uv) * p.bloom.rgb, p.bloom.w);

    const float ev = exposure_ev();
    color *= exp2(ev);

    if (p.vignette.w > 0.0)
    {
        float2 d = abs(uv - 0.5) * p.vignette.w;
        d.x *= lerp(1.0, p.screen.x * p.screen.w, p.lens.y);
        const float factor = pow(saturate(1.0 - dot(d, d)), p.lens.x);
        color *= lerp(p.vignette.rgb, float3(1.0), factor);
    }

    // white balance x color filter, contrast around middle gray (log space), hue shift x saturation
    color = float3(dot(p.balance_r.xyz, color), dot(p.balance_g.xyz, color), dot(p.balance_b.xyz, color));
    color = max(color, 0.0);
    if (p.tone.x != 1.0)
        color = MIDDLE_GRAY * pow(color / MIDDLE_GRAY, float3(p.tone.x));
    color = float3(dot(p.grade_r.xyz, color), dot(p.grade_g.xyz, color), dot(p.grade_b.xyz, color));

    float3 display = apply_tonemapper(uint(p.tone.y), max(color, 0.0));
    display = pow(saturate(display), float3(p.tone.z));

    // lift / gamma / gain
    display = p.gain.rgb * (display + p.lift.rgb * (1.0 - display));
    display = pow(saturate(display), p.gamma.rgb);

    // split toning: soft light of a color over the shadows and one over the highlights
    {
        const float t = saturate(luma(display) + p.split_shadows.w);
        display = soft_light(display, lerp(float3(0.5), p.split_shadows.rgb, 1.0 - t));
        display = soft_light(display, lerp(float3(0.5), p.split_highlights.rgb, t));
    }

    if (p.tone.w > 0.0)
        display = round(display * p.tone.w) / p.tone.w;

    // film grain: two octaves of value noise, new every frame, weaker in the highlights
    if (p.grain.x > 0.0)
    {
        const float2 seed = float2(p.grain.w * 17.17, p.grain.w * 31.31);
        const float2 cell = pixel / max(p.grain.y, 1.0) + seed;
        const float noise = value_noise(cell) + value_noise(cell * 2.03 + 11.7) - 1.0;
        const float response = lerp(1.0, 1.0 - luma(display), saturate(p.grain.z));
        display += noise * p.grain.x * 0.15 * response;
    }

    // triangular dither of 1 / 255: no banding in the 8 bit swapchain (sky gradients, fog)
    display += (hash12(pixel + p.grain.w * 3.7) + hash12(pixel + 101.3 + p.grain.w * 1.9) - 1.0) / 255.0;
    display = saturate(display);

    if (p.exposure.w > 0.5 && p.exposure.x > 0.5)
        display = draw_histogram(uv, display, ev);
    return display;
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
static const uint VIEW_GEOMETRY_NORMAL = 6;
static const uint VIEW_LINEAR_DEPTH    = 7;
static const uint VIEW_MOTION_VECTORS  = 8;
static const uint VIEW_EMISSIVE        = 9;
static const uint VIEW_AMBIENT_OCCLUSION = 10;

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
        case VIEW_AMBIENT_OCCLUSION:
            // GTAO result (denoised, its own resolution), without the material AO
            color = GTAO::visibility(uv).xxx;
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
    
    
    float3 mapped;
    if (pc.output_mode == 0)
    {
        mapped = post_process(input.uv, input.uv * PostProcess::params.screen.xy);
    }
    else
    {
        // GI debug outputs: exposure and tone curve only
        mapped = apply_tonemapper(uint(PostProcess::params.tone.y), hdr_color * exp2(exposure_ev()));
        mapped = pow(saturate(mapped), float3(PostProcess::params.tone.z));
    }

    mapped = DrawNumber9999(input.uv, mapped, pc.fps, float2(0.12), 0.08, float3(1.0, 0.2, 0.1));

    PSOutput output;
    output.color = float4(mapped, 1.0f);
    return output;
}
