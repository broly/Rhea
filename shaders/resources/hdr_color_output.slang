#pragma once

#include "hdr_color_defines.slang"

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

[[vk::binding(BINDING_SAMPLER_HDR_COLOR, SET_HDR_COLOR)]]
Sampler2D u_hdr_color_present[NUM_COLOR_OUTPUTS];

[[vk::binding(BINDING_SAMPLER_HISTORY, SET_HDR_COLOR)]]
Sampler2D u_hdr_color_history[NUM_COLOR_OUTPUTS];

float4 read_hdr_color_present<COLOR_OUTPUT_HDR output>(float2 uv)
{
    return u_hdr_color_present[(uint)output].Sample(uv);
}


float4 read_hdr_color_present<COLOR_OUTPUT_HDR output, int level>(float2 uv)
{
    return u_hdr_color_present[(uint)output].SampleLevel(uv, level);
}


float4 read_hdr_color_history<COLOR_OUTPUT_HDR output>(float2 uv)
{
    return u_hdr_color_history[(uint)output].Sample(uv);
}