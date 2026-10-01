// SET_GTAO, BINDING_SAMPLER_GTAO must be provided as compiler defines (layout: resources/gtao.glsl)
#ifndef SET_GTAO
    #error "SET_GTAO definition is missing. Provide this resource: gtao"
    #define SET_GTAO 0
#endif
#ifndef BINDING_SAMPLER_GTAO
    #error "BINDING_SAMPLER_GTAO definition is missing. Provide this resource: gtao"
    #define BINDING_SAMPLER_GTAO 0
#endif

namespace GTAO
{
    [[vk::binding(BINDING_SAMPLER_GTAO, SET_GTAO)]]
    internal Sampler2D u_gtao;

    // visibility of the hemisphere, 1: open. Bilinear inside the texture (the default sampler repeats: the
    // border would blend in the opposite one)
    public float visibility(float2 uv)
    {
        uint width, height;
        u_gtao.GetDimensions(width, height);
        const float2 half_texel = 0.5 / float2(width, height);
        return u_gtao.SampleLevel(clamp(uv, half_texel, 1.0 - half_texel), 0).r;
    }
}
