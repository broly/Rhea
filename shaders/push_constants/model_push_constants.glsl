#ifndef PUSH_CONSTANTS_MODEL
#define PUSH_CONSTANTS_MODEL

layout(push_constant) uniform ModelPushConstants
{
    mat4 model;
    mat4 prev_model;
    uvec4 indices;
} model_pc;

uint get_mesh_index()
{
    return model_pc.indices.x;
}

uint get_material_index()
{
    return model_pc.indices.z;
}

uint get_debug_index()
{
    return model_pc.indices.w;
}

#endif // PUSH_CONSTANTS_MODEL