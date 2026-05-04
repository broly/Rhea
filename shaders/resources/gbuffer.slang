#pragma once

// NUM_GBUFFER_SLOTS must be provided as a compiler define
#ifndef NUM_GBUFFER_SLOTS
    #error "NUM_GBUFFER_SLOTS not provided"
    #define NUM_GBUFFER_SLOTS 0
#endif

enum GBUFFER_SLOT
{
    GBUFFER_SLOT_NORMAL            = 0,
    GBUFFER_SLOT_WORLD_NORMAL      = 1,
    GBUFFER_SLOT_DEPTH             = 2,
    GBUFFER_SLOT_LINEAR_DEPTH      = 3,
    GBUFFER_SLOT_ALBEDO_ROUGHNESS  = 4,
    GBUFFER_SLOT_POSITION          = 5,
    GBUFFER_SLOT_MOTION_VECTORS    = 6,
    GBUFFER_SLOT_GEOMETRY_NORMAL   = 7,
    GBUFFER_SLOT_EMISSIVE          = 8,
}

static const uint GBUFFER_NUM_SLOTS = NUM_GBUFFER_SLOTS;

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

[[vk::binding(BINDING_SAMPLER_ARRAY_GBUFFER, SET_GBUFFER)]]
Sampler2D u_gbuffer[GBUFFER_NUM_SLOTS];

[[vk::binding(BINDING_SAMPLER_ARRAY_GBUFFER_HIST, SET_GBUFFER)]]
Sampler2D u_gbuffer_hist[GBUFFER_NUM_SLOTS];

float4 get_gbuffer<GBUFFER_SLOT buffer_id>(float2 uv)
{
    return u_gbuffer[(uint)buffer_id].Sample(uv);
}

float4 get_gbuffer_hist<GBUFFER_SLOT buffer_id>(float2 uv)
{
    return u_gbuffer_hist[(uint)buffer_id].Sample(uv);
}
