#ifndef RESOURCES_FOLIAGE_INSTANCES
#define RESOURCES_FOLIAGE_INSTANCES

// Ground foliage grown this frame (FoliageRenderer): GPU only buffers written by foliage_cull.comp, read by the
// indexed indirect draws of the pass GroundFoliage, one per bucket (type x variant x LOD).

#ifndef SET_FOLIAGE_INSTANCES
    #define SET_FOLIAGE_INSTANCES 0
    #error "SET_FOLIAGE_INSTANCES definition is missing. Provide this resource: foliage_instances"
#endif
#ifndef BINDING_SSBO_FOLIAGE_INSTANCES
    #define BINDING_SSBO_FOLIAGE_INSTANCES 0
    #error "BINDING_SSBO_FOLIAGE_INSTANCES definition is missing. Provide this resource: foliage_instances"
#endif
#ifndef BINDING_SSBO_FOLIAGE_ARGS
    #define BINDING_SSBO_FOLIAGE_ARGS 0
    #error "BINDING_SSBO_FOLIAGE_ARGS definition is missing. Provide this resource: foliage_instances"
#endif

struct FoliageInstance
{
    vec4 position_scale;    // xyz: root (world), w: scale (random x the distance fade)
    uvec4 info;             // x: seed, y: terrain normal xz (packSnorm2x16), z: bucket, w: -
};

// VkDrawIndexedIndirectCommand, one per bucket
struct FoliageDraw
{
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

#ifdef FOLIAGE_INSTANCES_WRITE
    #define FOLIAGE_INSTANCES_ACCESS
#else
    #define FOLIAGE_INSTANCES_ACCESS readonly
#endif

layout(std430, set = SET_FOLIAGE_INSTANCES, binding = BINDING_SSBO_FOLIAGE_INSTANCES)
FOLIAGE_INSTANCES_ACCESS buffer FoliageInstances
{
    FoliageInstance instances[];
} u_foliage_instances;

layout(std430, set = SET_FOLIAGE_INSTANCES, binding = BINDING_SSBO_FOLIAGE_ARGS)
FOLIAGE_INSTANCES_ACCESS buffer FoliageDraws
{
    FoliageDraw draws[];
} u_foliage_draws;

#endif // RESOURCES_FOLIAGE_INSTANCES
