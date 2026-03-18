#ifndef PUSH_CONSTANTS_MODEL
#define PUSH_CONSTANTS_MODEL

layout(push_constant) uniform ModelPushConstants
{
    uint mesh_id;
    uint transform_id;
    uint material_id;
    uint debug_id;
} model_pc;

uint get_mesh_index()
{
    return model_pc.mesh_id;
}

uint get_material_index()
{
    return model_pc.material_id;
}

uint get_transform_index()
{
    return model_pc.transform_id;
}

uint get_debug_index()
{
    return model_pc.debug_id;
}

#endif // PUSH_CONSTANTS_MODEL