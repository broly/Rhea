#ifndef RESOURCES_DRAW_COMMAND
#define RESOURCES_DRAW_COMMAND

// VkDrawIndexedIndirectCommand (DrawList::Command)
struct DrawCommand
{
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;   // the draw record
};

// world bounds of a draw record (DrawList::add_draw)
struct DrawBounds
{
    vec4 min;
    vec4 max;
};

#endif // RESOURCES_DRAW_COMMAND
