import hdr_color_defines;

// SET_HDR_COLOR, BINDING_SAMPLER_HDR_COLOR, BINDING_SAMPLER_HISTORY
// must be provided as compiler defines
#ifndef SET_HDR_COLOR
    #error "SET_HDR_COLOR definition is missing. Provide this resource: hdr_color_output"
    #define SET_HDR_COLOR 0
#endif
#ifndef BINDING_SAMPLER_HDR_COLOR
    #error "BINDING_SAMPLER_HDR_COLOR definition is missing. Provide this resource: hdr_color_output"
    #define BINDING_SAMPLER_HDR_COLOR 0
#endif
#ifndef BINDING_SAMPLER_HISTORY
    #error "BINDING_SAMPLER_HISTORY definition is missing. Provide this resource: hdr_color_output"
    #define BINDING_SAMPLER_HISTORY 0
#endif

namespace ColorOutput
{
    public float4 read<Slot output>(float2 uv)
    {
        return u_hdr_color_present[(uint)output].Sample(uv);
    }
    
    
    public float4 read<Slot output, int level>(float2 uv)
    {
        return u_hdr_color_present[(uint)output].SampleLevel(uv, level);
    }
    
    
    public float4 read_history<Slot output>(float2 uv)
    {
        return u_hdr_color_history[(uint)output].Sample(uv);
    }

    [[vk::binding(BINDING_SAMPLER_HDR_COLOR, SET_HDR_COLOR)]]
    internal Sampler2D u_hdr_color_present[NumOutputs];
    
    [[vk::binding(BINDING_SAMPLER_HISTORY, SET_HDR_COLOR)]]
    internal Sampler2D u_hdr_color_history[NumOutputs];

}

