#ifndef RESOURCES_DRAW_LIST
#define RESOURCES_DRAW_LIST

// Mesh draws of the frame (DrawList, src/game/draw_list.*): one record per drawn primitive, written by the CPU
// every frame. A draw's first_instance is the index of its record (indirect draws: VkDrawIndirectCommand,
// same buffer set, binding u_draw_commands - read by the command processor, not by shaders).

#ifndef SET_DRAW_LIST
    #define SET_DRAW_LIST 0
    #error "SET_DRAW_LIST definition is missing. Provide this resource: draw_list"
#endif
#ifndef BINDING_DRAW_RECORDS
    #define BINDING_DRAW_RECORDS 0
    #error "BINDING_DRAW_RECORDS definition is missing. Provide this resource: draw_list"
#endif

// GPUDrawRecord (src/render/ubos.ixx)
struct GPUDrawRecord
{
    uint mesh_id;        // mesh table entry (skinned primitives: their own copy)
    uint primitive_id;   // primitive table entry (transforms)
    uint material_id;    // material table entry of the pass
    uint user;           // pass specific (reflection probe capture: cube face)
};

layout(std430, set = SET_DRAW_LIST, binding = BINDING_DRAW_RECORDS)
readonly buffer DrawRecords
{
    GPUDrawRecord records[];
} u_draw_records;

// Location of the flat varying which carries the record index to the fragment shader
#define DRAW_RECORD_LOCATION 15

// DRAW_RECORD_NO_VARYING: vertex shaders whose fragment shader does not read the record (no unused output)
#ifdef FRAGMENT_SHADER
layout(location = DRAW_RECORD_LOCATION) flat in uint v_draw_record;

uint get_draw_record_index()
{
    return v_draw_record;
}
#else
#ifndef DRAW_RECORD_NO_VARYING
layout(location = DRAW_RECORD_LOCATION) flat out uint v_draw_record;
#endif

// gl_InstanceIndex = first_instance of the draw (every mesh draw has a single instance)
uint get_draw_record_index()
{
    return uint(gl_InstanceIndex);
}

// every vertex shader of a mesh draw calls it: the fragment shader reads the record through v_draw_record
void forward_draw_record()
{
#ifndef DRAW_RECORD_NO_VARYING
    v_draw_record = uint(gl_InstanceIndex);
#endif
}
#endif

GPUDrawRecord get_draw_record()
{
    return u_draw_records.records[get_draw_record_index()];
}

#endif // RESOURCES_DRAW_LIST
