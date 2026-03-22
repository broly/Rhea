#ifndef RESOURCES_GBUFFER
#define RESOURCES_GBUFFER

#ifndef NUM_GBUFFER_SLOTS
    #define NUM_GBUFFER_SLOTS 0
    #error "NUM_GBUFFER_SLOTS not provided"
#endif

const uint GBUFFER_SLOT_NORMAL = 0;
const uint GBUFFER_SLOT_WORLD_NORMAL = 1;
const uint GBUFFER_SLOT_ROUGHNESS = 2;
const uint GBUFFER_SLOT_DEPTH = 3;
const uint GBUFFER_SLOT_ALBEDO = 4;
const uint GBUFFER_SLOT_POSITION = 5;
const uint GBUFFER_SLOT_MOTION_VECTORS = 6;

const uint GBUFFER_NUM_SLOTS = NUM_GBUFFER_SLOTS;

#ifndef SET_GBUFFER
    #define SET_GBUFFER 0
    #error "SET_GBUFFER definition is missing"
#endif
#ifndef BINDING_SAMPLER_ARRAY_GBUFFER
    #define BINDING_SAMPLER_ARRAY_GBUFFER 0
    #error "BINDING_SAMPLER_ARRAY_GBUFFER definition is missing. Provide this resource: gbuffer"
#endif

layout(set = SET_GBUFFER, binding = BINDING_SAMPLER_ARRAY_GBUFFER) 
uniform sampler2D u_gbuffer[GBUFFER_NUM_SLOTS];


#endif  // RESOURCES_GBUFFER