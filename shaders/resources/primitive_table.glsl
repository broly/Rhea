#ifndef RESOURCES_PRIMITIVE_TABLE
#define RESOURCES_PRIMITIVE_TABLE

#ifndef SET_PRIMITIVE_TABLE
    #define SET_PRIMITIVE_TABLE 0
    #error "SET_PRIMITIVE_TABLE must be provided"
#endif
#ifndef BINDING_PRIMITIVE_TABLE
    #define BINDING_PRIMITIVE_TABLE 0
    #error "BINDING_PRIMITIVE_TABLE must be provided"
#endif

struct GPUPrimitiveInfo
{
    mat4 current_transform;
    mat4 prev_transform;
    uint mesh_id;
    uint material_id;
};

layout(std430, set=SET_PRIMITIVE_TABLE, binding=BINDING_PRIMITIVE_TABLE)
readonly buffer PrimitiveTable
{
    GPUPrimitiveInfo primitive_info_array[];
} u_primitive_table;

GPUPrimitiveInfo get_primitive_info(uint primitive_id)
{
    return u_primitive_table.primitive_info_array[nonuniformEXT(primitive_id)];
}

#endif  // RESOURCES_PRIMITIVE_TABLE