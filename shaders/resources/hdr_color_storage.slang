import hdr_color_defines;

#ifndef SET_HDR_COLOR_STORAGE
    #define SET_HDR_COLOR_STORAGE 0
    #error "SET_HDR_COLOR_STORAGE definition is missing. Provide this resource: hdr_color_storage"
#endif

#ifndef BINDING_SAMPLER_HDR_COLOR_STORAGE
    #define BINDING_SAMPLER_HDR_COLOR_STORAGE 0
    #error "BINDING_SAMPLER_HDR_COLOR_STORAGE definition is missing. Provide this resource: hdr_color_storage"
#endif


namespace ColorOutput
{
    public void write<Slot slot>(int2 point, float4 value)
    {
        u_hdr_color_present_storage[slot][point] = value;
    }
    
    public void write<Slot slot>(int2 point, float3 value)
    {
        u_hdr_color_present_storage[slot][point] = float4(value, 1.0);
    }

    [[vk::binding(BINDING_SAMPLER_HDR_COLOR_STORAGE, SET_HDR_COLOR_STORAGE)]]
    internal RWTexture2D<float4> u_hdr_color_present_storage[NumOutputs];
}