export module game:draw_list;

import std.compat;
import render;
import rhmath;
import glm;

// Mesh draws of the frame, in the frame's copy of the resource "draw_list" (host visible, rewritten every
// frame, shaders/resources/draw_list.glsl):
//   - records: what one draw draws (mesh, primitive, material, pass specific value). A draw's first_instance
//     is its record index: shaders read it through gl_InstanceIndex.
//   - commands: VkDrawIndexedIndirectCommand layout (20 bytes), consumed by RenderBackend::draw_indexed_indirect.
//     Mesh draws are indexed draws of the shared mesh index blocks (MeshIndexRange): the post transform vertex
//     cache only works for indexed draws. Draws of one indirect call share the bound index block.
// Passes group their draws by pipeline and index block: one indirect draw per group instead of one draw per
// primitive (only the draws that must keep their order across pipelines, like translucency, draw one by one).
//
//     const uint32_t first = draw_list.command_count();
//     for (...) draw_list.add_draw(record, index_range);
//     ctx.bind_pipeline(pipeline); ... draw_list.bind(ctx);
//     draw_list.draw_indirect(ctx, index_block, first, draw_list.command_count() - first);
export class DrawList
{
public:
    // layout of VkDrawIndexedIndirectCommand; non-indexed draws (add_vertex_draw) put a VkDrawIndirectCommand
    // into the first 16 bytes
    struct Command
    {
        uint32_t index_count;
        uint32_t instance_count;
        uint32_t first_index;
        int32_t vertex_offset;
        uint32_t first_instance;
    };

    void init(Renderer& renderer, RenderBackend& backend);

    // before the passes of a frame: the frame's buffers, counters reset (the frame's fence was waited)
    void begin_frame(RBFrameHandle frame);

    // record for a direct draw: draw it with ctx.draw(vertex_count, 0, <returned index>)
    uint32_t add_record(const GPUDrawRecord& record);

    // record + indirect command drawing the mesh indices (a single instance). The world bounds of the record
    // are for the draws the GPU culls (OcclusionCulling)
    void add_draw(const GPUDrawRecord& record, const MeshIndexRange& indices, const AABB* bounds = nullptr);
    // record + non-indexed command of vertex_count vertices (draw_vertex_indirect: shaders expanding primitives)
    void add_vertex_draw(const GPUDrawRecord& record, uint32_t vertex_count);

    uint32_t command_count() const { return num_commands; }

    // resource "draw_list" (compute passes reading the frame's draws)
    RenderResource* get_resource() const { return resource; }

    // the records of the frame, after binding the pipeline of the draws
    void bind(RenderGraphContext& ctx) const;

    // commands [first_command, first_command + count) of add_draw with the bound pipeline, all of index_block.
    // `commands` / command_offset: a GPU made copy of the frame's commands (occlusion culling), in their order
    void draw_indirect(RenderGraphContext& ctx, uint32_t index_block, uint32_t first_command, uint32_t count,
        std::optional<RBBufferHandle> commands = std::nullopt, uint32_t command_offset = 0) const;
    // the same for commands of add_vertex_draw
    void draw_vertex_indirect(RenderGraphContext& ctx, uint32_t first_command, uint32_t count) const;

private:
    RenderResource* resource = nullptr;
    RenderBackend* backend = nullptr;
    RBFrameHandle frame{};
    std::span<GPUDrawRecord> records;
    std::span<Command> commands;
    struct Bounds
    {
        glm::vec4 min;
        glm::vec4 max;
    };
    std::span<Bounds> bounds;
    RBBufferHandle commands_buffer{};
    uint32_t num_records = 0;
    uint32_t num_commands = 0;
};
