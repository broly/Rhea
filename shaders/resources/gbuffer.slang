module gbuffer;

// NUM_GBUFFER_SLOTS must be provided as a compiler define
#ifndef NUM_GBUFFER_SLOTS
    #error "NUM_GBUFFER_SLOTS not provided"
    #define NUM_GBUFFER_SLOTS 0
#endif


// SET_GBUFFER, BINDING_SAMPLER_ARRAY_GBUFFER, BINDING_SAMPLER_ARRAY_GBUFFER_HIST
// must be provided as compiler defines
#ifndef SET_GBUFFER
    #error "SET_GBUFFER definition is missing"
    #define SET_GBUFFER 0
#endif
#ifndef BINDING_SAMPLER_ARRAY_GBUFFER
    #error "BINDING_SAMPLER_ARRAY_GBUFFER definition is missing. Provide this resource: gbuffer"
    #define BINDING_SAMPLER_ARRAY_GBUFFER 0
#endif
#ifndef BINDING_SAMPLER_ARRAY_GBUFFER_HIST
    #error "BINDING_SAMPLER_ARRAY_GBUFFER_HIST definition is missing. Provide this resource: gbuffer"
    #define BINDING_SAMPLER_ARRAY_GBUFFER_HIST 0
#endif


namespace GBuffer
{
    public enum Slot
    {
        NORMAL            = 0,
        WORLD_NORMAL      = 1,
        DEPTH             = 2,
        LINEAR_DEPTH      = 3,
        ALBEDO_ROUGHNESS  = 4,
        POSITION          = 5,
        MOTION_VECTORS    = 6,
        GEOMETRY_NORMAL   = 7,
        EMISSIVE          = 8,
    }

    
    static const uint NUM_SLOTS = NUM_GBUFFER_SLOTS;
    
    [[vk::binding(BINDING_SAMPLER_ARRAY_GBUFFER, SET_GBUFFER)]]
    internal Sampler2D u_gbuffer[NUM_SLOTS];
    
    [[vk::binding(BINDING_SAMPLER_ARRAY_GBUFFER_HIST, SET_GBUFFER)]]
    internal Sampler2D u_gbuffer_hist[NUM_SLOTS];

    
    public uint num_slots()
    {
        return NUM_SLOTS;
    }
    
        
    public float4 get<Slot buffer_id>(float2 uv)
    {
        return u_gbuffer[(uint)buffer_id].Sample(uv);
    }
    
    public float4 get<Slot buffer_id, int level>(float2 uv)
    {
        return u_gbuffer[(uint)buffer_id].SampleLevel(uv, level);
    }
    
    public float4 get_hist<Slot buffer_id>(float2 uv)
    {
        return u_gbuffer_hist[(uint)buffer_id].Sample(uv);
    }
}

