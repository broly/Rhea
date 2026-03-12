#ifndef PUSH_CONSTANTS_MODEL
#define PUSH_CONSTANTS_MODEL

layout(push_constant) uniform ModelPushConstants
{
    mat4 model;
    mat4 prev_model;
    uint mesh_index;
} model_pc;

#endif // PUSH_CONSTANTS_MODEL