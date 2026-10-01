#ifndef RESOURCES_OCCLUSION
#define RESOURCES_OCCLUSION

// Occlusion culling of the base pass (OcclusionCulling, src/game/occlusion_culling.*): GPU only buffers, kept
// from frame to frame

#ifndef SET_OCCLUSION
    #define SET_OCCLUSION 0
    #error "SET_OCCLUSION definition is missing. Provide this resource: occlusion"
#endif
#ifndef BINDING_OCCLUSION_VISIBILITY
    #define BINDING_OCCLUSION_VISIBILITY 0
    #error "BINDING_OCCLUSION_VISIBILITY definition is missing. Provide this resource: occlusion"
#endif
#ifndef BINDING_OCCLUSION_COMMANDS
    #define BINDING_OCCLUSION_COMMANDS 0
    #error "BINDING_OCCLUSION_COMMANDS definition is missing. Provide this resource: occlusion"
#endif
#ifndef BINDING_OCCLUSION_HZB
    #define BINDING_OCCLUSION_HZB 0
    #error "BINDING_OCCLUSION_HZB definition is missing. Provide this resource: occlusion"
#endif

#include "resources/draw_command.glsl"

// by render primitive id: nonzero when it was visible at the end of the last frame
layout(std430, set = SET_OCCLUSION, binding = BINDING_OCCLUSION_VISIBILITY)
buffer OcclusionVisibility
{
    uint visible[];
} u_visibility;

// the base pass draws with their instance count set by the culling: the early draws, then the late ones
layout(std430, set = SET_OCCLUSION, binding = BINDING_OCCLUSION_COMMANDS)
buffer OcclusionCommands
{
    DrawCommand commands[];
} u_cull_commands;

// hierarchical depth: the farthest depth of every texel's area, mip levels one after the other (level 0 is a
// power of two of about half the screen)
layout(std430, set = SET_OCCLUSION, binding = BINDING_OCCLUSION_HZB)
buffer OcclusionHZB
{
    float depth[];
} u_hzb;

#endif // RESOURCES_OCCLUSION
