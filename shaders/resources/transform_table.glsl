#ifndef RESOURCES_TRANSFORM_TABLE
#define RESOURCES_TRANSFORM_TABLE

struct GPUTransform
{
    mat4 current_transform;
    mat4 prev_transform;
};

layout(std430, set=SET_TRANSFORM, binding=BINDING_TRANSFORM_TABLE)
readonly buffer TransformTable
{
    GPUTransform transforms[];
} u_transform_table;

GPUTransform get_transform(uint primitive_id)
{
    return u_transform_table.transforms[nonuniformEXT(primitive_id)];
}

#endif  // RESOURCES_TRANSFORM_TABLE