// Post processing parameters and buffers read by tonemap.slang (layout: resources/post_process.glsl).
// SET_POST_PROCESS, BINDING_UBO_POST_PROCESS, BINDING_SAMPLER_PP_BLOOM, BINDING_SAMPLER_PP_EXPOSURE
// must be provided as compiler defines (resource "post_process").
#ifndef SET_POST_PROCESS
    #error "SET_POST_PROCESS definition is missing. Provide this resource: post_process"
    #define SET_POST_PROCESS 0
#endif
#ifndef BINDING_UBO_POST_PROCESS
    #error "BINDING_UBO_POST_PROCESS definition is missing. Provide this resource: post_process"
    #define BINDING_UBO_POST_PROCESS 0
#endif
#ifndef BINDING_SAMPLER_PP_BLOOM
    #error "BINDING_SAMPLER_PP_BLOOM definition is missing. Provide this resource: post_process"
    #define BINDING_SAMPLER_PP_BLOOM 0
#endif
#ifndef BINDING_SAMPLER_PP_EXPOSURE
    #error "BINDING_SAMPLER_PP_EXPOSURE definition is missing. Provide this resource: post_process"
    #define BINDING_SAMPLER_PP_EXPOSURE 0
#endif

namespace PostProcess
{
    // PostProcessUBO (src/render/system_ubo.ixx)
    public struct Params
    {
        public float4 exposure;          // x: 1 auto exposure, y: manual EV, z: compensation EV, w: 1 draws the histogram
        public float4 auto_exposure;     // x: min EV, y: max EV, z: log2 of the key
        public float4 histogram;         // x: log2 luminance of the first bin, y: log2 luminance range
        public float4 bloom;             // rgb: tint, w: intensity
        public float4 balance_r;         // white balance x color filter
        public float4 balance_g;
        public float4 balance_b;
        public float4 grade_r;           // hue shift x saturation
        public float4 grade_g;
        public float4 grade_b;
        public float4 tone;              // x: contrast, y: tonemapper, z: 1 / display gamma, w: posterize levels
        public float4 lift;
        public float4 gamma;             // rgb: 1 / gamma
        public float4 gain;
        public float4 split_shadows;     // rgb: soft light color, w: balance
        public float4 split_highlights;
        public float4 vignette;          // rgb: color, w: intensity
        public float4 lens;              // x: vignette smoothness, y: vignette roundness, z: chromatic aberration, w: sharpen
        public float4 grain;             // x: intensity, y: size (px), z: response, w: frame seed
        public float4 screen;            // xy: size (px), zw: 1 / size
    };

    [[vk::binding(BINDING_UBO_POST_PROCESS, SET_POST_PROCESS)]]
    public ConstantBuffer<Params> params;

    [[vk::binding(BINDING_SAMPLER_PP_BLOOM, SET_POST_PROCESS)]]
    internal Sampler2D u_pp_bloom;

    [[vk::binding(BINDING_SAMPLER_PP_EXPOSURE, SET_POST_PROCESS)]]
    internal Sampler2D u_pp_exposure;

    public static const int HISTOGRAM_BINS = 64;

    // the end of the bloom chain (half the screen size), 3 x 3 tent: no blocky texels when upscaled
    public float3 bloom(float2 uv)
    {
        uint width, height;
        u_pp_bloom.GetDimensions(width, height);
        const float2 s = 1.0 / float2(width, height);
        float3 sum = u_pp_bloom.SampleLevel(uv, 0).rgb * 4.0;
        sum += (u_pp_bloom.SampleLevel(uv + float2(-s.x, 0.0), 0).rgb + u_pp_bloom.SampleLevel(uv + float2(s.x, 0.0), 0).rgb
              + u_pp_bloom.SampleLevel(uv + float2(0.0, -s.y), 0).rgb + u_pp_bloom.SampleLevel(uv + float2(0.0, s.y), 0).rgb) * 2.0;
        sum += u_pp_bloom.SampleLevel(uv + float2(-s.x, -s.y), 0).rgb + u_pp_bloom.SampleLevel(uv + float2(s.x, -s.y), 0).rgb
             + u_pp_bloom.SampleLevel(uv + float2(-s.x, s.y), 0).rgb + u_pp_bloom.SampleLevel(uv + float2(s.x, s.y), 0).rgb;
        return sum / 16.0;
    }

    // x: adapted exposure (EV), y: target of this frame, z: average log2 luminance, w: 1 once written
    public float4 exposure_state()
    {
        return u_pp_exposure.Load(int3(0, 0, 0));
    }

    // 0..1, the highest bin is 1
    public float histogram_bin(int bin)
    {
        return u_pp_exposure.Load(int3(clamp(bin, 0, HISTOGRAM_BINS - 1), 1, 0)).x;
    }
}
