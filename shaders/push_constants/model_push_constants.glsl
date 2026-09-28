#ifndef PUSH_CONSTANTS_MODEL
#define PUSH_CONSTANTS_MODEL

// Mesh draws: what is drawn comes from the draw record (resources/draw_list.glsl), the push constants only
// carry pass wide switches.
#include "resources/draw_list.glsl"

layout(push_constant) uniform ModelPushConstants
{
    uint debug_id;
} model_pc;

uint get_mesh_index()
{
    return get_draw_record().mesh_id;
}

uint get_material_index()
{
    return get_draw_record().material_id;
}

uint get_primitive_index()
{
    return get_draw_record().primitive_id;
}

// base pass: GeometryDebug bits (generic_render_graph.ixx)
uint get_debug_index()
{
    return model_pc.debug_id;
}

const uint GEOMETRY_DEBUG_ZERO_EMISSIVE = 1u;
const uint GEOMETRY_DEBUG_EMISSIVE_INDEX_CHECK = 2u;
const uint GEOMETRY_DEBUG_SOLID_EMISSIVE = 4u;
const uint GEOMETRY_DEBUG_GLOSSY = 8u;

#endif // PUSH_CONSTANTS_MODEL
